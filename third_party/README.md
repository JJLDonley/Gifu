# third_party/

Libraries Gifu builds with. They are not part of the repository: CMake downloads
them here on the first configure, at pinned versions checked against SHA-256
hashes (`cmake/Dependencies.cmake`):

| Library | Version | License | Source |
|---|---|---|---|
| raylib (with its bundled stb_truetype and sinfl) | 6.2 | zlib/libpng | https://github.com/raysan5/raylib/archive/refs/tags/6.2.tar.gz |
| Clay | 0.14 | zlib | https://github.com/nicbarker/clay/archive/refs/tags/v0.14.tar.gz |
| yyjson | 0.10.0 | MIT | https://github.com/ibireme/yyjson/archive/refs/tags/0.10.0.tar.gz |

Offline: extract each archive here yourself, named `raylib/`, `clay/` and
`yyjson/` (the archive's top folder renamed); a folder that is already present
is used as is.
