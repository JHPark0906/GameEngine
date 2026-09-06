# Asset database format

`AssetDatabase` indexes supported source files relative to a project package root. The root must
contain exactly one `<ProjectName>.gameproject` file; boot code reads that file first and the
database also records it as an asset for packaging and change detection.

## Importers

An importer decides both what an extension yields and what a particular file of that extension holds
inside it. `AssetImporterRegistry` maps an extension to one, and a file whose extension nothing
imports is not an asset. The engine registers these:

- root `.gameproject`: `ProjectSettings`
- `.scene`: `Scene`
- `.png`, `.jpg`, `.jpeg`: `Sprite`
- `.fbx`: `Mesh`
- `.ttf`, `.otf`: `Font`
- `.wav`, `.mp3`, `.ogg`: `AudioClip`

Extension matching is case-insensitive. A project adds a format with
`AssetImporterRegistry::Register`, which refuses an extension something already imports so that what
`.fbx` means cannot change silently.

The engine names its own importers in one table rather than having them register from static
initializers: the engine is a static library, and an object file nothing references can be dropped by
the linker, which would leave a format working in one application and missing in another.

## Sub-assets

A file holds one or more assets, and the importer for its extension says how many and what they are
called. Most formats hold exactly one, named after the file. A model file holds one mesh per geometry
it places, which is what `AssetReference`'s local id selects and why the count has to be known: a
reference to the second mesh in a file can only be offered, checked, or resolved by something that
knows the file holds two.

FBX local ids still follow import order. A file GUID preserves the file's identity, but cannot
preserve the meaning of `guid#N` when an export reorders or removes geometries. Existing sidecars
do not store the old sub-asset mapping, so changing that policy requires a deliberate migration:
capture each current local id against an unchanged source revision, persist the mapping, retain
removed ids as tombstones, and allocate new ids without reusing old ones. This release leaves the
reference format and local-id ordering unchanged rather than guessing at an existing reference.

The binary FBX parser rejects node nesting deeper than 128, more than 1,000,000 nodes or properties,
and more than 512 MiB of decoded array data per file. Node and property ranges must stay inside
their parent node, and compressed output may not exceed its declared decoded size. These limits
bound parser recursion and decoded-array allocation; they are not a total process-memory budget.

Importing runs when the database is refreshed, not when a frame draws, and a manifest carries the
result so a deployed player does not reimport a model to count its meshes. A file whose importer
fails is still registered, with no assets inside it — one unreadable model must not stop a project
from opening — and every reference into it then resolves to nothing.

Every database entry is represented by an `Asset` object that owns its original source path,
project-relative path, ID, hash, and file size. `ProjectSettingsAsset`, `SceneAsset`, `Sprite`,
`Mesh`, `Font`, and `AudioClip` carry type-specific behavior without exposing manifest records to
runtime consumers.

## Sprite import metadata

An image may have an adjacent `<image-name>.meta` import sidecar — `TX Tileset Ground.png.meta`.
The original file's extension says which kind of metadata it is, so the name carries no kind of
its own. It is source-only metadata and is embedded into the generated asset manifest rather than
copied into a package.

```json
{
  "format": "gameengine-meta/1",
  "pixelsPerUnit": 100.0,
  "border": [16.0, 16.0, 16.0, 16.0]
}
```

Border order is left, top, right, bottom in source pixels. Missing metadata uses 100 pixels per
unit and a zero border. A `SpriteRenderer` in `sliced` mode preserves these border regions while
stretching its center to the component's requested size.

The earlier name was `<image-name>.sprite.json`. A file under that name is still read, and doing so
logs the exact rename it wants, because projects live outside this repository and cannot all be
renamed at once.

## Identity and change detection

An adjacent `.meta` sidecar supplies the asset's persistent `guid`; its folded value is the in-memory
`AssetKey`. Keep the sidecar with the source when moving or renaming it. Development assets without
a GUID fall back to a 64-bit FNV-1a hash of the normalized, case-insensitive project-relative path,
with a warning, and remain addressable by path. Packaging requires every asset to have a GUID.

`contentHash` is a separate 64-bit FNV-1a hash of the file contents. A manifest is rejected when its
recorded size or content hash does not match the package, allowing the build system to detect stale
asset data.

## Manifest

The JSON manifest contains a `formatVersion` and a path-sorted `assets` array. IDs, hashes, and file
sizes are strings so their full integer precision is preserved by the JSON parser.

Format version 2 added `Scene` records, version 3 added `Font` records, version 4 introduced
polymorphic assets plus embedded Sprite import metadata, version 5 added each record's
`subAssets` array — the assets the importer found inside that file, in local-id order — and version
6 stores persistent GUID identities. Older
manifests are rejected so a packaged runtime cannot silently operate with an incomplete asset
database.

```json
"subAssets": [
  {"type": "Mesh", "name": "unicorn"}
]
```

Packaged builds store the manifest at `Assets/AssetDatabase.json`. Runtime startup validates and
loads that manifest when present. Development outputs without a manifest continue to scan the
project root so source assets can be iterated without running the packaging step after every edit.
An invalid packaged manifest is a hard failure and never falls back to a scan.
