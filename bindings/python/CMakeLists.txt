# Use CMake's FindPython, which locates headers from the active interpreter
# (a uv-managed or otherwise non-system one included).
set(PYBIND11_FINDPYTHON ON)

# pybind11 comes from the build environment (pyproject.toml), not from vcpkg:
# vcpkg's port drags in its own Python, which fights the interpreter the wheel
# is built for.
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(quantmicrostructure module.cpp)

target_link_libraries(quantmicrostructure PRIVATE quantMicrostructure)

qm_set_warnings(quantmicrostructure)

install(TARGETS quantmicrostructure
  LIBRARY DESTINATION .
  RUNTIME DESTINATION .
)
