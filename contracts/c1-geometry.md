# Contract C1: Geometry Core Semantics

Version: 1.0.0
Status: Locked (M0 Foundation)
Producers: `core/geometry`
Consumers: `core/document`, `core/rendering`, `core/typography`, `modules/vector`, `modules/production`, `modules/interop`

---

## 1. Coordinates and Units

- **Standard Measurement Unit**: Millimeters (`mm`).
- **Precision Representation**: IEEE-754 binary64 (`double`), minimum 53 bits significand.
- **World Canvas Bounds**: $[-1{,}000{,}000.0, +1{,}000{,}000.0]\text{ mm}$ ($1{,}000\text{ meters}$ across), accommodating large-scale signage and architecture.
- **Axis Orientation**:
  - World logical space: Cartesian coordinate system with origin $(0, 0)$ at bottom-left of canvas/artboard.
  - $+X$ points Right.
  - $+Y$ points Up.
  - Viewport rendering mapping: The renderer projects world $(X, Y)$ into viewport space, converting to local camera-relative floats with $+Y$ pointing Down for display rasterizers.

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
  A matrix is invertible if and only if $|\det(M)| > \varepsilon$ ($\varepsilon = 10^{-12}$).
- Precision Tolerance: Numerical error on round-trip transform inversion $(M \times M^{-1})$ must remain below $10^{-8}\text{ mm}$.

---

## 3. Curve Primitives

Paths are composed of contiguous subpaths of directional segments:
1. **Line Segment**: Start point $P_0$, end point $P_1$.
2. **Cubic Bézier Segment**: Start point $P_0$, first control point $P_1$, second control point $P_2$, end point $P_3$.
   $$B(t) = (1-t)^3 P_0 + 3(1-t)^2 t P_1 + 3(1-t) t^2 P_2 + t^3 P_3, \quad t \in [0, 1]$$
3. **Quadratic Bézier Segment**: Start point $P_0$, control point $P_1$, end point $P_2$.
4. **Elliptic Arc Segment**: Defined by radii $(r_x, r_y)$, x-axis rotation angle $\theta$, large-arc flag, sweep flag, and target point $P_1$. Must be preserved as analytic arc representations for CAD/manufacturing exports rather than pre-flattened polygons.

---

## 4. Fill and Winding Rules

Every filled path explicitly specifies its winding evaluation rule:
- **`Winding::NonZero`**: A ray cast from the point to infinity counts signed crossings (clockwise $+1$, counter-clockwise $-1$). Point is inside if winding sum $\ne 0$.
- **`Winding::EvenOdd`**: A ray cast counts total boundary crossings regardless of direction. Point is inside if count is odd.

---

## 5. Numerical Validation & Tolerances

- **Finite Validation**: Every coordinate, dimension, and transform component must satisfy `std::isfinite(val)`. Non-finite values (`NaN`, `Inf`) are rejected at the boundary with an error code, never propagated.
- **Geometric Tolerance ($\varepsilon$)**:
  - Point coincidence: $\|P_1 - P_2\| \le 10^{-6}\text{ mm}$ ($1\text{ nanometer}$).
  - Collinearity / flatness: Distance to chord $\le 10^{-5}\text{ mm}$.
  - Angular tolerance: $\le 10^{-7}\text{ radians}$.
- **Degenerate Handling**: Zero-length segments, empty bounding boxes, and self-intersecting polygons must produce explicit diagnostics or valid degenerate bounds rather than throwing unhandled exceptions or causing infinite recursion.
