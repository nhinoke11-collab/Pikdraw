# Contract C1: Geometry Core Semantics

Version: 1.1.1 (documentation correction; no geometry/storage API change)
Audited: 2026-10-24 against the working source.
Status: geometry primitives and EditablePath exist; operation-specific requirements below are not a claim that every tool is implemented or accepted.
Producers: `core/geometry`
Consumers: `core/document`, `core/rendering`, `core/typography`, `modules/vector`, `modules/production`, `modules/interop`

---

## 1. Coordinates and Units

- **Standard Measurement Unit**: Millimeters (`mm`).
- **Precision Representation**: IEEE-754 binary64 (`double`), minimum 53 bits significand.
- **World Coordinate Validation Bounds**: $[-1{,}000{,}000.0, +1{,}000{,}000.0]\text{ mm}$ per axis ($2{,}000\text{ meters}$ across). These are `validate_coordinate` bounds, not a guarantee that every importer, page, render target or algorithm accepts that full range.
- **Axis Orientation**:
  - World logical space: Cartesian coordinates; the default page spans from $(0,0)$ at bottom-left to $(width,height)$. An artboard or object can have its own offset/transform.
  - $+X$ points Right.
  - $+Y$ points Up.
  - Viewport mapping flips Y for display; precision boundaries of the current Direct2D backend are described in [C3](c3-rendering.md).

---

## 2. Affine Transforms (Transform2D)

A 2D affine transformation is represented as a $3 \times 3$ column-vector transformation matrix:

$$\begin{bmatrix} x' \\ y' \\ 1 \end{bmatrix} = \begin{bmatrix} a & c & tx \\ b & d & ty \\ 0 & 0 & 1 \end{bmatrix} \begin{bmatrix} x \\ y \\ 1 \end{bmatrix}$$

- Forward mapping:
  $$x' = a \cdot x + c \cdot y + tx$$
  $$y' = b \cdot x + d \cdot y + ty$$
- Identity: $a=1, b=0, c=0, d=1, tx=0, ty=0$.
- Composition Order: $M_{\text{combined}} = M_{\text{parent}} \times M_{\text{child}}$ (child transformation applied first to local geometry, followed by parent coordinate space transformation).
- Determinant & Invertibility:
  $$\det(M) = a \cdot d - b \cdot c$$
  Validate finite components first; `Transform2D` uses $|\det(M)| > \varepsilon$ with default $\varepsilon = 10^{-12}$. Domain-specific transforms such as gradients can use a relative guardrail instead.
- Named inversion round-trip tolerance: $10^{-8}\text{ mm}$ in `validation.hpp`; this is a predicate/test target, not a universal error guarantee for ill-conditioned inputs.

---

## 3. Curve Primitives

Paths are composed of contiguous subpaths of directional segments:
1. **Line Segment**: Start point $P_0$, end point $P_1$.
2. **Cubic Bézier Segment**: Start point $P_0$, first control point $P_1$, second control point $P_2$, end point $P_3$.
   $$B(t) = (1-t)^3 P_0 + 3(1-t)^2 t P_1 + 3(1-t) t^2 P_2 + t^3 P_3, \quad t \in [0, 1]$$
3. **Quadratic Bézier Segment**: Start point $P_0$, control point $P_1$, end point $P_2$.
4. **Elliptic Arc Segment**: External endpoint syntax may use radii $(r_x,r_y)$, axis rotation, large-arc and sweep flags, and target point. Convert that syntax to the canonical analytic parameterization before creating a source command; do not store a renderer-flattened polygon as source.

The canonical parameterization is

$$P(t)=C+U\cos(\alpha_0+t\Delta\alpha)+V\sin(\alpha_0+t\Delta\alpha),\quad t\in[0,1]$$

where $C$ is a point and $U,V$ are independent axis vectors in the path's local mm frame. After an affine transform $x'=Ax+T$, transform $C'=AC+T$, $U'=AU$, and $V'=AV$; keep the parameter interval. $U,V$ need not remain orthogonal after non-uniform scale or shear. Reflection changes orientation through the transformed basis and determinant; it must not be approximated by a line or a fixed polygon. Singular transforms and invalid/collinear endpoint-arc inputs return an explicit diagnostic.

---

## 4. Fill and Winding Rules

Every filled path explicitly specifies its winding evaluation rule:
- **`WindingRule::NonZero`**: A ray cast counts oriented crossings. Point is inside if winding sum $\ne 0$; the overall sign convention does not change membership.
- **`WindingRule::EvenOdd`**: A ray cast counts total boundary crossings regardless of direction. Point is inside if count is odd.

---

## 5. Numerical Validation & Tolerances

- **Finite Validation**: Every coordinate, dimension, and transform component must satisfy `std::isfinite(val)`. Non-finite values (`NaN`, `Inf`) are rejected at the boundary with an error code, never propagated.
- **Geometric Tolerance ($\varepsilon$)**:
  - Point coincidence: $\|P_1 - P_2\| \le 10^{-6}\text{ mm}$ ($1\text{ nanometer}$).
  - Collinearity / flatness: Distance to chord $\le 10^{-5}\text{ mm}$.
  - Angular tolerance is operation-specific; there is no shared `1e-7` angular constant in the current validation header.
- **Degenerate Handling**: Zero-length segments, empty bounding boxes, and self-intersecting polygons must produce explicit diagnostics or valid degenerate bounds rather than throwing unhandled exceptions or causing infinite recursion.

The values above are named baseline predicates, not a universal epsilon for every algorithm. New geometry queries carry an explicit tolerance profile in model units or screen DIP; point identity comes from IDs, not coordinate proximity. Hit testing and snap acquisition use screen-space DIP. Curve flattening uses a view-dependent pixel error. Boolean/offset topology uses its own bounded model-space error and reports exhaustion. User-requested join/merge carries an explicit threshold. A caller may not silently replace one tolerance class with another.

## 6. Editable path and construction geometry semantics (extension 1.1)

- Source coordinates and path-local transforms remain binary64 mm with world +Y up. A local construction frame has an origin and orthonormal X/Y axes expressed in world coordinates. A positive angle is counter-clockwise from that frame's +X; camera rotation and screen Y inversion do not change geometry angles.
- `EditablePath` stores ordered subpaths, anchor nodes, and source segments. Segment kinds are line, quadratic Bézier, cubic Bézier, and analytic elliptic arc. Each source element has a stable typed ID. `Path2D`/flattened geometry is a derived read-only adapter for existing consumers, never a second writable authority.
- Cubic control points are the outgoing handle of the start node and incoming handle of the end node. **Stored handles are absolute control-point positions in the path-local mm frame**, not anchor-relative vectors. Compute a vector as `handle - anchor`; writing a vector into a handle field corrupts geometry. Quadratic controls remain on the segment. An absent handle evaluates to its anchor in the adapter.
- Current validation: Corner and AutoSmooth reject explicitly stored handles; Cusp permits independent handles; Smooth requires two opposing collinear nonzero handles; Symmetric also requires equal lengths. These are geometric tangency constraints, not C2 continuity. AutoSmooth derives a tangent from neighboring anchors and lengths from adjacent chords divided by three, scaled by tension in [0,1]. End nodes use their only adjacent chord. An undefined tangent returns the anchor for both controls; the adapter does not emit a separate diagnostic. A tool that edits a derived handle must explicitly choose a compatible mode before storing controls.

The following topology rules are requirements for each implementing command. Check that command and its regression coverage before advertising the operation; this section does not assert universal tool coverage.

- A close operation never duplicates the first node or creates a zero-length segment. If the user targets the first node ID, the final anchor is merged into that target, the first ID survives, and an ID map is returned; the last source segment becomes the closing edge. Otherwise create one explicit closing line from last to first, or reject an exact zero-length closure with guidance to target/merge the first node. Joining separated paths is a Bridge operation. Merging endpoints is always explicit and returns the selected survivor ID.
- Split uses a source parameter $t$ strictly inside (0,1), preserves exact source geometry (De Casteljau for Béziers and parameter interval subdivision for arcs), preserves endpoint IDs and the original segment ID for the leading segment, and returns the new node and trailing-segment IDs. Any topology edit returns before/after ID mappings; it never silently reuses a deleted ID.

## 7. Source authority and remaining work

- [Transform2D](../core/geometry/include/pikdraw/geometry/primitives/transform.hpp) and [validation constants](../core/geometry/include/pikdraw/geometry/primitives/validation.hpp).
- [EditablePath validation](../core/document/include/pikdraw/document/model/editable_path.hpp) and [Path2D adapter/AutoSmooth](../core/document/include/pikdraw/document/model/path_adapter.hpp).
- General parametric/construction source types and a shared construction-frame API remain design requirements where no corresponding runtime type exists. Do not infer them from this contract's terminology.
- This audit changes documentation only. Large-coordinate GPU precision, topology coverage and production tolerances require operation-specific evidence.
