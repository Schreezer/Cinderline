# Source review

Reviewed the frozen Headquarters rally and automatic mining delta on 2026-09-13.

- `AutoRally` reset mode requires a pinned, completed, owned producer. A Headquarters can restore automatic mining without a team army flag; combat producers still require that flag.
- Manual and automatic Headquarters rally changes clear deferred worker assignment candidates and their cursor before the completed paid job is reconsidered.
- A Headquarters rally placed on an explored, nonempty ore deposit prefers that exact deposit, while the worker assignment still requires reachable outward and return routes. An unusable deposit falls back to the ordinary rally movement behavior.
- Protocol 5 validates reset mode, translates the opaque Headquarters handle, and preserves the authoritative override through owned snapshots without exposing enemy rally state.
- Save schema 8 persists the Headquarters override and deferred worker plan. Its required rally section and current invariants remain compatible with the new reset behavior.
- The pinned JOBS panel exposes `AUTO MINE` only when a Headquarters has an override, and dispatches the same authoritative reset command.

No actionable defect was found in this bounded review. No build, test, or app run was performed here.
