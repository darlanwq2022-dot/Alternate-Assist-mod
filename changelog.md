# Changelog

## v0.1.1 - Build fix
- Fixed Geode 5.11 setting listeners by explicitly specifying each setting's value type.
- Boolean settings use `bool`; the profile uses `std::string`; float settings use `double`; the integer CPS setting uses `int64_t`.
- No controller behavior or gameplay input logic was intentionally changed.

## v0.1.0
- Initial adaptive Ship assistance implementation.
