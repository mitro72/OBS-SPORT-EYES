OBS Sport Eyes v1.10.0c – Director AI GUI restore

Replace these two files over v1.10.0b:
- src/filter/SportEyesFilterProperties.cpp
- src/filter/SportEyesFilterLifecycle.cpp

This restores a visible, checkable Director AI group and maps its properties to
filter_data::directorAIEnabled and DirectorAIConfig. Turning the group on/off or
changing its tuning values resets the engine history safely.
