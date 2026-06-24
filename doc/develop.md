# Building and developing microtext

`build/build.sh` builds, tests, and runs every artifact into `build/output`.

```sh
build/build.sh test       # render samples and check them (exits non-zero on failure)
build/build.sh sanitize   # the test under AddressSanitizer + UBSan
build/build.sh leaks      # the test under the macOS leaks tool
build/build.sh demo1      # build the headless gallery demo
build/build.sh run-demo1  # build and render every feature to build/output/showcase.png
build/build.sh demo2      # build the interactive raylib demo (needs brew raylib)
build/build.sh run-demo2  # build and open the raylib demo window
```
