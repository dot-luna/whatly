# Embedded settings checks

Build and run the settings bridge and browser regression tests with the same
dependencies as the application, plus Qt Test:

```sh
cmake -S . -B build-tests -G Ninja -DWHATLY_TESTS=ON
cmake --build build-tests --parallel 4 --target tst_embeddedsettingsbridge
ctest --test-dir build-tests -R '^embedded_settings$' --output-on-failure
```

The tests isolate settings and browser data from personal profiles. They check
the bundled form against the real Qt settings handlers, including persistence,
secret redaction, input validation, appearance, and navigation restrictions.

The native-window regression opens the panel through the actual injected
navigation entry and WebChannel, then checks pointer movement, editing, page
visibility changes, resizing, and explicit dismissal. External browser requests
are blocked; a local synthetic layout stands in for the signed-in website.

The fixtures in `fixtures/` exercise the production JavaScript and CSS with
synthetic WhatsApp layouts and a mock settings bridge. Navigation covers row
placement, profile access, hover-triggered redraws, remounts, resizing,
right-to-left layout, narrow windows, and unavailable hosts. Form checks cover
category navigation, search, and wide and narrow layouts.
These checks do not require a WhatsApp login or verify the current signed-in
WhatsApp DOM. For manual inspection, serve the repository root locally and
open either fixture in a browser; results are in `window.testResults`.
