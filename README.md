# PDFable

A PDF reader and annotator for Windows pen devices, written in C++17 with Qt 6 Widgets and MuPDF.

## What it does

- **Reading**: continuous scrolling, zoom (Ctrl+wheel, pinch, fit width/page), outline sidebar, text search,
  text selection and copy, back/forward after link jumps, printing, password-protected files.
- **Everyday comfort**: reopens the files from the last session at the page and zoom where you left them;
  reloads a file automatically when another program rewrites it (LaTeX); a second start, e.g. double-clicking a
  PDF in Explorer, opens the file as a tab in the running window; page themes Light, Sepia and Dark (dark grey
  paper, light text, colours keep their hue; Ctrl+I toggles); full screen (F11); copy any area of a page as an
  image (Ctrl+Shift+C); adjustable scroll speed (Edit > Preferences); back/forward also for scroll positions
  (Alt+Left/Right, mouse back button).
- **Pen, mouse, finger**: the pen and the mouse each keep their own tool. Tap a toolbar button with the pen to
  set the pen's tool, click it with the mouse for the mouse's tool. When the pen is used, the mouse returns to
  selecting/navigating. A finger only pans and zooms.
- **PDF Creator** (File > Create / merge PDF, or the "PDFable Creator" shortcut, or `PDFable.exe --creator`):
  drop PDFs and images, then join whole files, arrange single pages by hand, or interleave pages (zipper).
- **Tabs and windows**: several documents in tabs; drag a tab out of the bar to get a new window, drop it on
  another window's tab bar to move it there.
- **Link previews**: hover an internal link (citation, figure, equation, section) and a pop-up shows the target
  region of the document. Move the pointer into the pop-up to scroll, select, copy or highlight there.
  Click the link to jump, Alt+Left to return.
- **Pen**: pressure-sensitive ink with jitter filtering and curve smoothing. Hold the pen still at the end of a
  stroke and it becomes a straight line, rectangle or ellipse; a snapped line can still be dragged by its end.
  Rectangles and ellipses can be resized the same way: keep the pen down after the snap and drag.
  Six colour presets per tool (right-click or double-click a swatch to change it) and a continuous size slider.
  Highlighter (multiply blend), stroke eraser (also the pen's eraser end and barrel button), palm rejection,
  finger panning. Ink is stored as standard PDF ink annotations, so other readers show it too.
- **Other annotations**: a quick bar next to a text selection highlights, underlines, strikes out or copies
  with one click; text boxes typed directly on the page (double-click to edit, drag a corner to change the
  font size); images. The Select tool
  moves and resizes ink, text boxes and images; Del removes them. Undo/redo for everything.
- **Handwritten signatures**: draw once, keep in a library, place and resize on any page (Sign menu).
- **Page editing**: drag thumbnails to reorder; rotate, delete, extract, insert pages from another PDF, insert
  blank pages, merge files, save an optimised (smaller) copy.

Certificate-based signing (PAdES / CAdES with PKCS#12 IDs, timestamps, certification, validation) is implemented
in `src/core/Signer.cpp` but switched off by default. Enable it with `-DPDFABLE_SIGNING=ON` and the vcpkg feature
`signing` (adds OpenSSL).

## Development (WSL)

Development happens in WSL (Ubuntu) with the sources in this folder, which WSL sees as
`/mnt/c/Users/<you>/Desktop/Claude/PDFable`. One script does everything:

    cd /mnt/c/Users/<you>/Desktop/Claude/PDFable
    ./dev.sh setup      # once: apt packages, Qt 6.8.3 (aqtinstall), MuPDF 1.28.6 from source
    ./dev.sh build      # Linux build in ~/.cache/pdfable/build-linux
    ./dev.sh test       # headless self-test
    ./dev.sh run        # start PDFable; WSLg shows the window on the Windows desktop
    ./dev.sh appimage   # dist/PDFable-<version>-x86_64.AppImage
    ./dev.sh windows    # Windows build with Visual Studio, dist/PDFable-<version>-windows-x64.zip
    ./dev.sh all        # all of the above

Both platforms use the same Qt (6.8.3) and MuPDF (1.28.6) versions. VS Code with the WSL extension
picks up `CMakePresets.json` (preset "linux"), which points at the same folders as `dev.sh`.

The Windows build needs Visual Studio 2022 Build Tools ("Desktop development with C++") and vcpkg in
`C:\vcpkg`; Qt is downloaded automatically. `./dev.sh windows` runs `build.bat` through WSL interop;
double-clicking `build.bat` on Windows does the same.

## Packages

- **Windows:** `dist\PDFable-<version>-windows-x64.zip`. Unzip anywhere and start `PDFable.exe`, or
  `PDFable Creator.exe` for the creator. Nothing to install: Qt and the Visual C++ runtime are inside.
  The unpacked folder is also in `dist\PDFable`.
- **Linux:** `dist/PDFable-<version>-x86_64.AppImage`. `chmod +x` it and run it on any distribution
  at least as new as the one it was built on. `PDFable-*.AppImage --creator` opens the creator.

## Status

Automated headless test in `tests/tests.cpp` (`./dev.sh test`). Pen and touch input are tested by hand on
Windows.

Known gaps: text selection is per page; form filling is not
implemented; extracting or merging pages keeps ink and comments but drops links and form fields; saving
resets the undo history.

## Shortcuts

V select, H hand, P pen, M highlighter, E eraser, T text box, Ctrl+F find, Ctrl+H highlight selection,
Ctrl+Shift+H handwritten signature, F4 sidebar, Ctrl+1/2/0 fit page / fit width / 100 %, Alt+Left/Right history.

## Licence note

MuPDF is AGPL-3.0 (or commercial). That is fine for personal use; distributing binaries means publishing the
source under AGPL-compatible terms.
