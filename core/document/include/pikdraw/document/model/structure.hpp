#pragma once
#include "pikdraw/document/model/object.hpp"

namespace pikdraw::document {
class PersistentObjectMap;
// Exactly one parent per source object; all roots belong to a layer, groups are acyclic.
void validate_document_structure(const Page& page, const PersistentObjectMap& objects);
}
