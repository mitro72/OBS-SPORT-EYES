#include "sport-eyes-filter-internal.h"

static bool areClose(const cv::Rect2f &a, const cv::Rect2f &b, float maxDist)
{
    cv::Point2f ca(a.x + a.width * 0.5f, a.y + a.height * 0.5f);
    cv::Point2f cb(b.x + b.width * 0.5f, b.y + b.height * 0.5f);
    return cv::norm(ca - cb) < maxDist;
}

bool sport_eyes_build_group_bbox(const std::vector<Object> &objects,
                           cv::Rect2f &outBox,
                           int minPeople,
                           float maxDist)
{
    for (size_t i = 0; i < objects.size(); ++i) {
        if (objects[i].unseenFrames > 0)
            continue;

        cv::Rect2f groupBox = objects[i].rect;
        int count = 1;

        for (size_t j = 0; j < objects.size(); ++j) {
            if (i == j || objects[j].unseenFrames > 0)
                continue;

            if (areClose(objects[i].rect, objects[j].rect, maxDist)) {
                groupBox |= objects[j].rect;
                count++;
            }
        }

        if (count >= minPeople) {
            outBox = groupBox;
            return true;
        }
    }
    return false;
}

// --- Group clustering helpers (multiple clusters) ---
// GroupCluster is declared in sport-eyes-filter-internal.h because the crop
// pipeline consumes the selected component.

// Full clustering (used for preview): returns all clusters (count>=minPeople), sorted by area desc.
static std::vector<GroupCluster> buildGroupClusters(const std::vector<Object> &objects,
							    int minPeople,
							    float maxDist)
{
	std::vector<size_t> visibleIdx;
	visibleIdx.reserve(objects.size());
	for (size_t i = 0; i < objects.size(); ++i) {
		if (objects[i].unseenFrames == 0)
			visibleIdx.push_back(i);
	}

	std::vector<GroupCluster> clusters;
	if (visibleIdx.empty())
		return clusters;

	// Reuse buffer to avoid per-frame allocations (CPU spikes)
	static std::vector<uint8_t> visited;
	visited.assign(objects.size(), 0);

	std::vector<size_t> stack;
	stack.reserve(visibleIdx.size());

	for (size_t seedPos = 0; seedPos < visibleIdx.size(); ++seedPos) {
		size_t seed = visibleIdx[seedPos];
		if (visited[seed])
			continue;

		// BFS/DFS over "close" graph (transitive)
		stack.clear();
		stack.push_back(seed);
		visited[seed] = 1;

		cv::Rect2f box = objects[seed].rect;
		int count = 0;

		while (!stack.empty()) {
			size_t u = stack.back();
			stack.pop_back();
			++count;
			box |= objects[u].rect;

			// Compare only against not-yet-visited visible nodes.
			for (size_t vPos = 0; vPos < visibleIdx.size(); ++vPos) {
				size_t v = visibleIdx[vPos];
				if (visited[v])
					continue;
				if (areClose(objects[u].rect, objects[v].rect, maxDist)) {
					visited[v] = 1;
					stack.push_back(v);
				}
			}
		}

		if (count >= std::max(1, minPeople)) {
			GroupCluster c;
			c.box = box;
			c.count = count;
			clusters.push_back(c);
		}
	}

	// Prefer biggest cluster first (useful for selecting boundingBox / consistent labels)
// Basket-friendly ordering: first by people count (desc), then by area (desc).
	std::sort(clusters.begin(), clusters.end(), [](const GroupCluster &a, const GroupCluster &b) {
		if (a.count != b.count)
			return a.count > b.count;
		return a.box.area() > b.box.area();
	});

	return clusters;
}

// Best-cluster selection (used for crop/tracking):
// - avoids storing/sorting all clusters
// - early-exit when a cluster contains *all* visible people (cannot be beaten by count)
bool sport_eyes_select_best_group_cluster(const std::vector<Object> &objects,
					  int minPeople,
					  float maxDist,
					  GroupCluster &bestOut)
{
	std::vector<size_t> visibleIdx;
	visibleIdx.reserve(objects.size());
	for (size_t i = 0; i < objects.size(); ++i) {
		if (objects[i].unseenFrames == 0)
			visibleIdx.push_back(i);
	}
	if (visibleIdx.empty())
		return false;

	static std::vector<uint8_t> visited;
	visited.assign(objects.size(), 0);

	std::vector<size_t> stack;
	stack.reserve(visibleIdx.size());

	bool found = false;
	float bestArea = -1.0f;

	for (size_t seedPos = 0; seedPos < visibleIdx.size(); ++seedPos) {
		const size_t seed = visibleIdx[seedPos];
		if (visited[seed])
			continue;

		stack.clear();
		stack.push_back(seed);
		visited[seed] = 1;

		cv::Rect2f box = objects[seed].rect;
		int count = 0;

		while (!stack.empty()) {
			size_t u = stack.back();
			stack.pop_back();
			++count;
			box |= objects[u].rect;

			for (size_t vPos = 0; vPos < visibleIdx.size(); ++vPos) {
				size_t v = visibleIdx[vPos];
				if (visited[v])
					continue;
				if (areClose(objects[u].rect, objects[v].rect, maxDist)) {
					visited[v] = 1;
					stack.push_back(v);
				}
			}
		}

		if (count >= std::max(1, minPeople)) {
			const float area = box.area();
			if (!found || count > bestOut.count || (count == bestOut.count && area > bestArea)) {
			bestOut.box = box;
			bestOut.count = count;
			bestArea = area;
			found = true;
		}
			// EARLY-EXIT (crop): if this cluster contains everyone visible, it's maximal by count.
			if (count == (int)visibleIdx.size()) {
				return true;
			}
		}
	}

	return found;
}

void sport_eyes_draw_group_clusters(cv::Mat &frame,
			      const std::vector<Object> &objects,
			      int minPeople,
			      float maxDist,
			      bool showLabel)
{
	auto clusters = buildGroupClusters(objects, minPeople, maxDist);
	if (clusters.empty())
		return;

	// Style: thicker stroke for clusters
	const int thickness = 3;
	for (size_t i = 0; i < clusters.size(); ++i) {
		const auto &c = clusters[i];
		cv::Rect r = c.box;
		// Clamp to image bounds (avoid OpenCV assertions)
		r &= cv::Rect(0, 0, frame.cols, frame.rows);
		if (r.width <= 0 || r.height <= 0)
			continue;

		cv::rectangle(frame, r, cv::Scalar(255, 0, 255), thickness);

		if (!showLabel)
			continue;

		// Label: "G1 (N)" — build string only when enabled
		const std::string label = "G" + std::to_string(i + 1) + " (" + std::to_string(c.count) + ")";
		int baseline = 0;
		auto ts = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.7, 2, &baseline);
		const int x = std::max(0, r.x);
		const int y = std::max(ts.height + 4, r.y);
		cv::rectangle(frame, cv::Rect(x, y - ts.height - 4, ts.width + 6, ts.height + 6),
			      cv::Scalar(0, 0, 0), -1);
		cv::putText(frame, label, cv::Point(x + 3, y),
			    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
	}
}

