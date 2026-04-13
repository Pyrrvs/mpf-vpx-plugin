# Notes on porting mpf-vpx-plugin to VPX master ABI

**Status:** Deferred. We currently target the latest stable VPX release tag (`v10.8.1-3788-2151290`). VPX master has a different plugin SDK ABI that our plugin does not yet compile against. This document captures what we know so we can pick this up when the next stable VPX release is cut (or when we decide to ship against master).

## Why we're deferring

1. The current stable tag is what users actually install. Shipping against master would require users to download a VPX nightly.
2. The master ABI is a moving target — every commit to master could potentially break our build.
3. Our `Changed*` return type fix (Option B with object + VBScript helper) works within the current stable ABI, so we don't need master to ship a working plugin.
4. We have an upstream PR pending that adds string/bool array support to `DynamicScript.cpp`. Once that PR lands and the next stable release ships, we can revisit.

## ABI differences between the tagged release and master

### 1. Plugin SDK header location

| Source | Path |
|--------|------|
| Tag `v10.8.1-3788-2151290` | `src/plugins/MsgPlugin.h`, `src/plugins/ScriptablePlugin.h`, etc. |
| Master | `plugins/plugins/MsgPlugin.h`, `plugins/plugins/ScriptablePlugin.h`, etc. |

Our `CMakeLists.txt` already auto-detects this:

```cmake
if(EXISTS "${vpx-sdk_SOURCE_DIR}/plugins/plugins/MsgPlugin.h")
    set(VPX_PLUGIN_INCLUDE_DIR "${vpx-sdk_SOURCE_DIR}/plugins")
elseif(EXISTS "${vpx-sdk_SOURCE_DIR}/src/plugins/MsgPlugin.h")
    set(VPX_PLUGIN_INCLUDE_DIR "${vpx-sdk_SOURCE_DIR}/src")
endif()
```

So include resolution itself is not the blocker — the macro signatures are.

### 2. PSC macro signatures

This is the actual blocker. The PSC macros on master changed shape; our existing code uses the older form.

**`PSC_CLASS_START`:**

```cpp
// Tag (v10.8.1-3788-2151290) — 1 argument, class context implicit
PSC_CLASS_START(MPF_Controller)

// Master — 2 arguments, second is the C++ type the script class binds to
PSC_CLASS_START(MPF_Controller, MPFController)
```

On the tag, the implementer (Task 7) had to add `using MPF_Controller = MPFController;` so the macro could resolve casts back to `MPFController`. On master, the second macro argument provides this binding directly via a `_BindedClass` typedef — no `using` alias needed.

**`PSC_FUNCTION0`/`PSC_FUNCTION1`/`PSC_FUNCTION2`:**

```cpp
// Tag — 3 arguments (class, return type, name) for FUNCTION0; (class, return type, name, arg) for FUNCTION1; etc.
PSC_FUNCTION0(MPF_Controller, void, Run)
PSC_FUNCTION1(MPF_Controller, void, PulseSW, int)
PSC_FUNCTION2(MPF_Controller, void, Run, string, int)

// Master — 2 arguments (return type, name) for FUNCTION0; (return type, name, arg) for FUNCTION1; etc.
// Class context comes from the enclosing PSC_CLASS_START via `_BindedClass`
PSC_FUNCTION0(void, Run)
PSC_FUNCTION1(void, PulseSW, int)
PSC_FUNCTION2(void, Run, string, int)
```

### 3. CI evidence

When CI was first set up to build against both the tag and master, the master jobs failed with:

```
src/MPFPlugin.cpp:18: error: macro "PSC_CLASS_START" requires 2 arguments, but only 1 given
src/MPFPlugin.cpp:20: error: macro "PSC_FUNCTION0" passed 3 arguments, but takes just 2
src/MPFPlugin.cpp:21: error: macro "PSC_FUNCTION1" passed 4 arguments, but takes just 3
```

This is why the build matrix in `.github/workflows/build.yml` currently targets only the tagged release.

## What needs to happen to port

### Source code changes (per file)

**`src/MPFPlugin.cpp`** — All 30+ PSC macro invocations need to be rewritten:

1. Drop the `using MPF_Controller = MPFController;` alias
2. Change `PSC_CLASS_START(MPF_Controller)` to `PSC_CLASS_START(MPF_Controller, MPFController)`
3. Change every `PSC_FUNCTION{0,1,2}(MPF_Controller, retType, name, ...)` to `PSC_FUNCTION{0,1,2}(retType, name, ...)`
4. Change every `PSC_PROP_*(MPF_Controller, type, name)` to `PSC_PROP_*(type, name)`
5. Change every `PSC_PROP_*_ARRAY1(MPF_Controller, type, name, idx)` to `PSC_PROP_*_ARRAY1(type, name, idx)`
6. The manual `members.push_back(...)` blocks for Mech/GetMech use `static_cast<_BindedClass*>(me)` — keep that, it works on master too via the START macro's typedef

**`src/MPFController.h`** — May need to drop or update `PSC_IMPLEMENT_REFCOUNT()` depending on whether master changed that macro too. Verify by checking the master `ScriptablePlugin.h`.

**`CMakeLists.txt`** — No changes needed; the auto-detection of include path already handles both layouts. Only update the default `VPX_TAG` once a new stable release is out that includes both the master ABI and our upstream `DynamicScript.cpp` patch.

**`.github/workflows/build.yml`** — Add `master` back to the matrix once the source compiles against it:

```yaml
vpx_tag: ["<new-stable-tag>", "master"]
```

### Estimated effort

- ABI port itself: ~1 hour. The macro signature changes are mechanical — find/replace + verify each one compiles. Roughly 30 macro calls in `MPFPlugin.cpp`.
- Verify `PSC_IMPLEMENT_REFCOUNT()` still works the same way: 15 minutes of reading the master `ScriptablePlugin.h`.
- Test that the runtime behavior is unchanged: 30 minutes of running the existing test suite + smoke testing in VPX.
- Total: ~2 hours.

The risk is that master may have added more changes we haven't catalogued (e.g. type system changes, settings registration changes, lifecycle changes). Worth re-reading the PinMAME plugin on master before starting — it's the canonical reference implementation.

## Trigger conditions for doing this work

Pick this up when **any** of:

- A new stable VPX release ships that includes our upstream `DynamicScript.cpp` string-array patch.
- VPX upstream removes the old macro signatures (we'd be forced to port).
- We need a feature that's only available on master.
- A user reports they want to use the plugin with a master build.

## Related

- Upstream patch tracking: see `docs/superpowers/notes/2026-04-13-vpx-string-array-patch.md` (TBD — populate when PR is filed).
- Current return-type workaround: see the upcoming spec for the `Changed*` object-based API + VBScript helper layer.
