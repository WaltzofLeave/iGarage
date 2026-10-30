# Icon sources and permitted use

Reviewed on 2026-10-30. The inventory and SHA-256 hashes are in
`assets/icons/manifest.json`; the portable build includes the same inventory,
original SVG files, complete Apache license and third-party notices.

## JetBrains UI icons

104 SVG files, including light and dark variants, are taken from the official
[IntelliJ Platform icon collection](https://intellij-icons.jetbrains.design/),
at the upstream revision pinned in the manifest. Every imported file contains
an explicit Apache-2.0 license declaration and its original copyright notice.
The source bytes are unchanged. Apache-2.0 permits commercial use and
redistribution subject to its conditions; the license and upstream NOTICE are
included in `assets/icons/licenses/`.

The official [JetBrains announcement](https://blog.jetbrains.com/platform/2024/08/more-new-ui-icons-for-your-plugins/)
says that most, rather than all, icons carry this explicit license. Consequently,
the C, C++ and header-file candidates that lacked per-file declarations were
excluded from this import. They use the licensed generic file icon. No product
logos, proprietary PyCharm application resources or icons copied from an
installed IDE are included. The terminal uses the licensed shell-file artwork.

The renderer selects the supplied dark/light variant and may recolor its primary
ink for hover, selection or disabled states. These are runtime presentation
changes; the downloaded SVG files and their notices are retained unchanged.
Orbit's own application logo remains original artwork. JetBrains trademarks do
not become licensed by Apache-2.0, and Orbit does not claim JetBrains endorsement.

## Python file and interpreter identification

`python.svg` is the original, unaltered two-snakes SVG linked by the
[Python logo page](https://www.python.org/community/logos/).
It is not labeled Apache-2.0 or PSF-2.0: the manifest uses the separate
`LicenseRef-PSF-Logo` identifier and links its actual permission.

The [PSF trademark policy](https://www.python.org/psf/trademarks/#uses-that-never-require-approval)
explicitly permits unaltered logos to accurately identify software written in,
compatible with, or containing the Python programming language, in both
commercial and non-commercial contexts without prior approval. Orbit uses it
only for that identification, keeps the artwork's proportions, colors and
shadow, and renders an adjacent TM indicator. It does not use this mark as the
application logo, imply sponsorship, or incorporate it into a company/product
name. The attribution and these scope restrictions ship in
`assets/icons/licenses/Python-logo-NOTICE.txt`. A different use must be reviewed
against the PSF policy independently.

## Rendering and validation

`src/ui/icons.*` owns the typed icon registry, file/symbol mappings and a shared,
bounded GPU bitmap cache. Direct2D rasterizes the original SVG once for a given
physical size, theme and ink color. Workbench windows and pickers share that
cache; warmed drawing does not reopen files, parse SVG or rebuild geometry.
Native SVG support requires Windows 10 version 1703 or later.

The folding margin uses the official 9×9 `gutter/fold.svg` and `unfold.svg`
artwork, replacing Scintilla's wide outlined triangles. The margin remains
13 DIP wide and clickable. RGBA markers are regenerated for the current DPI
and theme and are centered by Scintilla; the code font and line height are
unchanged.

Run the offline provenance/license gate and native rendering tests:

```text
python tools/check_icon_licenses.py
app/OrbitNative.exe --icon-test
```

The native tests render every registered icon in both themes at 100%, 150% and
200% scale, check the Python colors, measure arrow bounds, exercise actual
Scintilla folding, verify the click area and check warmed cache reuse. This
does not replace inspection of the running workbench; visual observations and
screenshots are recorded separately in QA.
