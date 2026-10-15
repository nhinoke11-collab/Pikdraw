# PikDraw vector

Status: Implemented in Milestone M1 (ps10-vector-basics).

Responsibility:
- Model-Space Selection (`selection/selection_model.hpp`): selection tracking (Replace, Add, Toggle) and distance-tolerant hit testing in world mm.
- Shape Factory (`tools/shape_tools.hpp`): creation of PathObject rectangles, ellipses, and basic paths.
- Transform Gesture (`gestures/transform_gesture.hpp`): interactive pointer drag with transient preview and atomic commit or Escape cancel.
- Text Tool (`gestures/text_tool.hpp`): UpdateTextCommand preserving font identities and Vietnamese Unicode content (NFC/NFD).
- Style Tool (`gestures/style_tool.hpp`): SetObjectStyleCommand for fills, strokes, and stroke widths with undo/redo.
- Vector Controller (`vector_controller.hpp`): coordinates tool modes (Select, Rectangle, Ellipse, Text), selection state, and atomic delete. Zero Qt widget dependencies.

Dependencies: `core/document`, `core/geometry`, `core/color`, `core/typography`. Zero UI dependencies.
Tests: 3 test suites passing (`test_pikdraw_vector_selection`, `test_pikdraw_vector_shapes`, `test_pikdraw_vector_gestures`).
