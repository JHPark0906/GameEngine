# Scene JSON format

`gameObjects` is a flat storage array; runtime Transform hierarchy is represented with
optional numeric `id` and `parent` members on each GameObject. Loading is performed in two
passes, so a parent may appear before or after its children. IDs must be unique and every
`parent` value must refer to an object with an `id`. Missing `parent` means a scene root.

```json
{
  "gameObjects": [
    { "id": 1, "name": "Parent", "components": [{ "type": "Transform" }] },
    { "id": 2, "parent": 1, "name": "Child", "components": [{ "type": "Transform" }] }
  ]
}
```

Transform `position`, `rotation`, and `scale` values are local to the parent. Rotation is an
Euler `[x, y, z]` vector measured in degrees: X is pitch, Y is yaw, and Z is roll. The engine
uses `Matrix4x4::CreateRotationRollPitchYawDegrees` with the roll-pitch-yaw convention. `rotationUnit` may be omitted or set to
`"degrees"`; `rotationOrder` may be omitted or set to `"rollPitchYaw"`. Other conventions
are rejected instead of being interpreted silently.

The legacy Transform `children` member may only be absent or an empty array. Use GameObject
`id`/`parent` references for hierarchy.

Component `type` values are resolved through `ComponentFactory`. Runtime and application
modules register their own construction callbacks before loading a scene, so adding a
component does not require changing `SceneSerializer`. Engine types are listed in
[`EngineComponentTypes`](RuntimeComponentFactories.cpp); project types participate through
`RegisterComponentType(T::StaticType())`. Properties are loaded and cloned within
`PropertyRestoreScope`, then `OnPropertiesRestored` validates dependent values such as
camera clipping planes.

A `type` no factory is registered for — a project-defined component opened in a process
that does not link the project, such as the editor — is preserved rather than dropped: the
component's JSON is kept as data on the object and written back as JSON on save, so a
scene edited elsewhere loses nothing and the component comes back to life in any process
that does register the factory. A registered type whose factory fails is still a warning
and is not preserved by the unknown-component fallback.

`UIWindow` is a registered concrete engine component. Its serialized form is
`{ "type": "UIWindow" }`; it has no window-specific serialized properties. In particular,
`SetModal` controls transient runtime state: modal state is not saved and a loaded window
starts non-modal. The window's separate `RectTransform` defines its pointer occlusion area,
and `parent`/`id` define which controls and nested windows belong to it.

```json
{
  "gameObjects": [
    { "id": 1, "name": "Canvas", "components": [{ "type": "Canvas" }] },
    { "id": 2, "parent": 1, "name": "Window", "components": [
      { "type": "RectTransform", "offsetMin": [0, 0], "offsetMax": [200, 100] },
      { "type": "UIWindow" }
    ] }
  ]
}
```

Input and screen rendering share `BuildUIStack`: background controls precede windows,
a window's ordinary descendants precede its nested window branches, and the active modal
branch is placed above other windows. There is no serialized window z-order or modal flag.
Runtime sibling operations change this hierarchy order; they do not introduce a JSON field.
See [`UIWindow`](../Runtime/UIWindow.cpp) and the
[runtime architecture](../../Docs/ARCHITECTURE.md).

`TextRenderer` accepts UTF-8 `text` and selects explicitly registered font bytes by the
`fontFamily` alias. The engine text rasterizer does not search installed system fonts.
`fontSize`, `color`, `alignment` (`left`, `center`, or `right`), `maxWidth`, and `lineSpacing`
control layout. `backgroundColor` and `backgroundPadding` add a background to the text block.
`space` may be `screen` or `world`; world text uses `pixelsPerUnit` to convert glyph pixels
to world units. Screen text without a RectTransform uses the Transform in render-target
pixels. With a RectTransform it uses the resolved layout rectangle, horizontal/vertical
alignment, and the enclosing Canvas scale.

`SpriteRenderer.drawMode` is `simple` or `sliced`. Simple mode derives its size from the Sprite
asset's image dimensions and pixels-per-unit value in world space. Sliced mode uses the
component's `size` in its draw space and preserves the Sprite asset's left/top/right/bottom
import borders. A screen sprite with a RectTransform uses its resolved layout size; a
placed screen sprite with no Sprite asset produces a solid rectangle.

`Light` lights the meshes of the scene. `kind` is `directional` (shines along the Transform's
+Z axis; position is ignored), `point` (shines from the Transform's position out to `range` world
units), or `ambient` (added to every surface regardless of direction). `color` and `intensity`
multiply. A frame carries at most three directional/point lights, taken in instance order; a
scene with no collected directional/point or ambient light receives the frontend's default
ambient and directional lighting. Ambient components accumulate separately from the
three directional/point slots.
