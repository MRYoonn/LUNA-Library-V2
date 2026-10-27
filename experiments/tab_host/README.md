# Two-document Windows tab-host experiment

This separate executable is **not production LUNA**, is not included in its
portable ZIP, and does not modify PDF4QT. The default LUNA path stays unchanged.
HTML v0.9 remains the final UI target; the experimental host is not a visual
implementation of that design.

Build with MSVC and the same Qt version as the selected LUNA package:

```
cmake -S experiments/tab_host -B tab-host-build -DCMAKE_PREFIX_PATH=<Qt>
cmake --build tab-host-build --config Release
luna-tab-host --method qt <LUNA.exe> <A.pdf> <B.pdf>
luna-tab-host --method win32 <LUNA.exe> <A.pdf> <B.pdf>
```

Use disposable PDF copies only. Manual mode leaves tab closing to the native
Editor close/save dialog. Host close is refused while any child is running.
It does not kill unsaved child processes on normal host shutdown. Host crashes,
foreign-window destruction, modal ownership and focus remain acceptance risks.

The synthetic-only Python driver compares Qt `fromWinId/createWindowContainer`
and Win32 `SetParent` at Qt scale 1/1.25/1.5. It records actual screenshots,
source-byte preservation, tab switching, resizing, deliberate crash of synthetic
A while B survives, and clean close of B. Only the synthetic automatic driver
may exercise the deliberate crash; never use --report with user documents.
A lifecycle success is **not POC success**: every report says `pocAccepted:false`.
Do not convert that flag until native input, Alt+Tab, shortcuts, annotation/save
isolation, native dialogs, close/save/discard/cancel tests pass.
Physical Windows monitor DPI is distinct from Qt scale factors.

Do not ship this experiment or substitute tab buttons that merely activate
separate top-level windows. A failed experiment preserves the independent-window
baseline and is evidence for architecture review, not a reason to weaken tests.
