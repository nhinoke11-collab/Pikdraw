#include "pikdraw/document/document.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace pikdraw::document;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

PathObject object(ObjectId id) {
    PathObject result;
    result.id = id;
    result.path = pikdraw::geometry::Path2D::make_rect(0, 0, 10, 10);
    return result;
}

class FailingMutation final : public DocumentCommand {
public:
    explicit FailingMutation(bool throws) : throws_(throws) {}
    std::string_view name() const noexcept override { return "Injected partial mutation"; }
    CommandResult execute(Document& doc) override {
        auto page = doc.page();
        page.width_mm = 42;
        doc.apply_mutation(page, doc.objects());
        if (throws_) throw std::runtime_error("injected command exception");
        return CommandResult::fail("injected failure after mutation");
    }
    CommandResult undo(Document&) override { return CommandResult::fail("not a rollback oracle"); }
private:
    bool throws_;
};

void document_case(const std::string& name) {
    Document doc("audit-document");
    if (name == "redo") {
        for (ObjectId id = 1; id <= 3; ++id)
            require(doc.execute_command(std::make_unique<AddObjectCommand>(object(id))).success, "add");
        require(doc.undo() && doc.undo() && doc.undo(), "three undos");
        require(doc.redo() && doc.redo() && doc.redo(), "redo chain lost after first redo");
        require(doc.current_snapshot()->object_count() == 3, "redo must restore all objects");
    } else if (name == "atomic_failure" || name == "atomic_exception") {
        const auto before = doc.current_snapshot();
        const auto result = doc.execute_command(std::make_unique<FailingMutation>(name == "atomic_exception"));
        require(!result.success, "injected mutation must fail");
        require(doc.page().width_mm == before->page().width_mm, "failed command changed writer state");
        require(doc.revision() == 0 && !doc.can_undo(), "failed command changed revision/history");
        require(doc.current_snapshot() == before, "failed command published a snapshot");
    } else if (name == "duplicate_id") {
        require(doc.execute_command(std::make_unique<AddObjectCommand>(object(7))).success, "add");
        const auto before = doc.current_snapshot();
        require(!doc.execute_command(std::make_unique<AddObjectCommand>(object(7))).success,
                "duplicate ID overwrites source object");
        require(doc.current_snapshot() == before, "duplicate ID must leave state untouched");
    } else if (name == "deletion_order") {
        for (ObjectId id = 1; id <= 3; ++id)
            require(doc.execute_command(std::make_unique<AddObjectCommand>(object(id))).success, "add");
        const auto order = doc.page().layers.front().object_ids;
        require(doc.execute_command(std::make_unique<RemoveObjectCommand>(2)).success, "remove");
        require(doc.undo(), "undo delete");
        require(doc.page().layers.front().object_ids == order, "undo delete changes z-order");
    } else if (name == "foreign_document") {
        AsyncProposal proposal;
        proposal.metadata = {"another-document", doc.epoch(), doc.revision(), 8, 0, {}};
        proposal.commands.push_back(std::make_unique<AddObjectCommand>(object(9)));
        require(doc.validate_and_commit_proposal(std::move(proposal)) != ProposalStatus::Accepted,
                "foreign document proposal accepted");
        require(doc.revision() == 0, "foreign job mutated document");
    } else if (name == "lifecycle_epoch") {
        Document reopened("audit-document");
        require(doc.epoch() != reopened.epoch(), "reopened document reuses lifecycle epoch");
    } else {
        throw std::runtime_error("unknown document case: " + name);
    }
}

void fidelity_case(const std::string& name);

int main(int argc, char** argv) {
    try {
        require(argc == 2, "one case argument required");
        const std::string name = argv[1];
        if (name == "source_color" || name == "source_font" || name == "boolean_curve" ||
            name == "offset_curve" || name == "svg_units" || name == "render_fill")
            fidelity_case(name);
        else
            document_case(name);
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
