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
source-byte preservation, tab switching, resizing, actual Win32 mouse/Ctrl+F/text/
Escape input and document-owned search popup results, Alt+Tab out-and-back
using an owned synthetic helper window, deliberate crash of synthetic
A while B survives, and clean close of B. Only the synthetic automatic driver
may exercise the deliberate crash; never use --report with user documents.
After lifecycle checks, the driver also runs the existing native Editor
annotation/save/reopen regression inside embedded A while B stays alive.
These checks use Qt test events and fixture dialogs; real Win32 input and native
modal dialogs still require separate evidence.

A lifecycle success is **not POC success**: every report says `pocAccepted:false`.
Do not convert that flag until native input, Alt+Tab, shortcuts, annotation/save
isolation, native dialogs, close/save/discard/cancel tests pass.
Physical Windows monitor DPI is distinct from Qt scale factors.

Do not ship this experiment or substitute tab buttons that merely activate
separate top-level windows. A failed experiment preserves the independent-window
baseline and is evidence for architecture review, not a reason to weaken tests.

## Architecture decision boundary

| Candidate | Isolation and integration | Work remaining before production |
| --- | --- | --- |
| One Editor process per document, Qt foreign-window container | Existing controllers/plugins stay in separate processes; Qt manages container geometry. Focus and native popup ownership still cross process boundaries. | Prove input, dialogs, close cancellation and DPI; add explicit document-state/close acknowledgements before a host can remove a tab. |
| One Editor process per document, Win32 parenting | Same engine-state isolation; LUNA explicitly controls child styles, visibility and size. | The same safety gates, plus Windows-specific restoration of styles/parent, focus and DPI behavior. A successful SetParent call alone is insufficient. |
| Several Editor instances in one process | Widgets naturally fit a QStackedWidget, but process isolation is lost. | Audit plugin and global state first. The pinned controller obtains QPluginLoader::instance() and rebinds loaded plugins with setWidget(); constructing another controller does not establish independent plugin instances. This is not a safe drop-in replacement. |
| Independent windows with activation buttons | Preserved baseline with separate native document state. | Useful fallback during development, but does not meet the approved single-window tab interface. |

Even if the experiment passes, embedding the entire existing Editor is not the
final interface: the production design needs one shared library, document-only
child surfaces, active-document action routing, dirty-state reporting and a
close/save/discard/cancel acknowledgement protocol. A tab must remain present
until its own Editor confirms closure. Switching tabs must never reopen a PDF.
These are LUNA overlay responsibilities; this experiment does not change the
pinned PDF4QT source or introduce another PDF renderer.

Experiments against preserved Build 20 (source 50692ee) establish results only
for that validated baseline. They cannot validate later library/UI changes.
The latest application regression and the experimental-host source SHA must be
reported separately. Physical monitor DPI and Qt process scale are also
separate acceptance items.
