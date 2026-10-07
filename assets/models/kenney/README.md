# Aetheris Demo World — Kenney Prototype Kit

Aetheris maps the Demo World slots to low-poly Prototype Kit model stems.

Expected imported files:

- floor-square.obj
- wall.obj
- shape-slope.obj
- stairs.obj
- crate.obj
- column.obj
- floor-thick.obj

The importer is deliberately bounded and runs during editor/project boot only. Missing or invalid files are not fatal: DemoWorldInitializer generates equivalent low-poly fallback geometry.

Kenney's Prototype Kit is released under CC0, so it can be used in personal and commercial projects without attribution. Developers should obtain the current package from the official Kenney Prototype Kit page and place their selected editable/model exports in this directory before launching the editor.

The runtime render thread never parses model files. Imported mesh data is converted into static device-local vertex/index buffers before frame production begins.