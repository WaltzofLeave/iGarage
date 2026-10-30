# Orbit changes to Scintilla 5.6.7

The upstream License.txt and copyright notices are retained.

- `win32/SurfaceD2D.cxx`: load the bundled JetBrains Mono font through a cached private DirectWrite font collection, without installing system fonts.
- `win32/ScintillaWin.cxx`: use `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` for the DirectWrite1 renderer and provide the dirty rectangle to Present1. This resolves blank presentation observed on the validation machine.
- The legacy HWND target helper specifies local bitmap remoting. Orbit itself selects `SC_TECHNOLOGY_DIRECT_WRITE_1` rather than that legacy renderer.

Recheck rendering, partial repaint, selection, caret, scrolling and resizing before changing the pinned upstream version.
