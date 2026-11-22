// SceneDocument.hpp
// The scene the editor has open: its file, its materials, the selection, undo, and play mode.
//
// Undo works on the whole scene as text: after every finished edit the scene is written
// to a string and compared with the previous one. That is simple and it cannot miss a
// kind of change, because what is saved is exactly what a file would hold.
#pragma once

#include "MoltenMetal.hpp"

#include <functional>
#include <string>
#include <vector>

namespace molten::editor {

/// Sizes a box, sphere or capsule collider to the mesh its entity draws, at the entity's scale
/// (colliders ignore Transform::scale: the scale goes into their size). A flat mesh, a plane,
/// gets a slab half a meter thick under it, with its top where the plane is.
void fitColliderToMesh(Collider& collider, const AABB& meshBounds, Vec3 scale);
/// True if the collider is the size fitColliderToMesh would give it at that scale.
bool isColliderFitted(const Collider& collider, const AABB& meshBounds, Vec3 scale);

enum class Primitive { Empty, Cube, Sphere, Plane, Cylinder, DirectionalLight, PointLight, SpotLight, Camera, ParticleEmitter };

class SceneDocument {
public:
    explicit SceneDocument(Engine& engine) : engine_(engine) {}

    // ---- the file

    /// The scene's file, absolute. Empty for a scene that has never been saved.
    std::string filePath;
    std::vector<NamedMaterial> materials;

    void newScene();
    /// What a new scene holds, as the text of a scene file: for making one on disk
    /// without touching the scene that is open.
    std::string defaultSceneText();
    bool open(const std::string& absolutePath, std::string* error = nullptr);
    /// Saves to filePath.
    bool save(std::string* error = nullptr);
    bool saveAs(const std::string& absolutePath, std::string* error = nullptr);
    /// The scene differs from what was opened or last saved.
    bool isDirty() const { return current_ != saved_; }
    /// The scene as text, as of the last finished edit: what Save would write. For the autosave.
    const std::string& currentText() const { return current_; }
    /// Replaces the scene with what an autosave kept: the file stays as it is, the scene has changes
    /// to save, and Undo goes back to the scene as the file has it.
    bool recover(const std::string& sceneText);
    /// "Main" for ".../Scenes/Main.scene", "Untitled" without a file.
    std::string displayName() const;

    // ---- the selection. The last entity is the "primary" one: the gizmo sits on it.

    const std::vector<Entity>& selection() const { return selection_; }
    Entity primary() const { return selection_.empty() ? Entity{} : selection_.back(); }
    bool isSelected(Entity entity) const;
    void select(Entity entity);                 // only this one; an invalid entity clears
    void addToSelection(Entity entity);
    void toggleSelection(Entity entity);
    void clearSelection() { selection_.clear(); }
    void selectAll();

    // ---- entities

    /// Everything in the scene except the editor's own entities, in file order. Refreshed by beginFrame().
    const std::vector<Entity>& entities() const { return entities_; }
    /// Entities without a parent, and the children of one, in the same order.
    std::vector<Entity> roots() const;
    std::vector<Entity> children(Entity parent) const;
    bool isDescendant(Entity entity, Entity ancestor) const;
    std::string nameOf(Entity entity) const;

    Entity create(Primitive kind, Entity parent = Entity{});
    Entity createFromMesh(const std::string& meshName, Vec3 position);
    /// Writes an entity and everything under it to a prefab file: a piece of scene that
    /// can be put in any scene, or spawned by the game while it runs.
    bool savePrefab(Entity root, const std::string& absolutePath);
    /// Adds a prefab's entities to the scene, the root at `position`. `assetName` is the
    /// prefab's path in Assets. Returns the root.
    Entity instantiatePrefab(const std::string& assetName, Vec3 position);
    /// Copies an entity and everything under it. Returns the copy.
    Entity duplicate(Entity entity);
    void duplicateSelection();
    /// Removes the selected entities and everything under them.
    void deleteSelection();

    // ---- what the Edit and Entity menus do to the selection

    /// The top of each selected tree: a selected child of a selected parent comes along with it anyway.
    std::vector<Entity> selectionTops() const;
    /// The selected trees as scene text, for the clipboard; empty with nothing selected.
    std::string copySelection();
    /// Adds what copySelection() wrote, beside `beside` (under its parent) or at the top of the
    /// scene, and selects it. Empty if the text is not entities.
    std::vector<Entity> paste(const std::string& text, Entity beside = Entity{});
    /// True if a text is something paste() takes.
    static bool isEntityText(const std::string& text);
    /// Everything under what is selected joins the selection.
    void selectChildren();
    /// What was selected is not, and the rest is.
    void invertSelection();
    /// An empty entity in the middle of the selected trees, which become its children. It is selected.
    Entity groupSelection();
    /// The selected trees leave their parents and stay where they are in the world.
    void clearParentOfSelection();
    /// The selected trees go first, or last, among their siblings.
    void moveSelectionInOrder(bool first);
    /// Switches the selected on if any is off, off otherwise.
    void toggleSelectionActive();
    /// Puts the selection where the view looks: at its pivot, each thing keeping its place among the others.
    void moveSelectionToView();
    /// An asset was renamed or moved: the scene names it by its new path from now on. The file
    /// moved on the disk, so this is not a step to undo, and it does not make the scene "changed".
    void retargetAsset(const std::string& from, const std::string& to);
    /// Gives `child` another parent (an invalid entity for none) without moving it in the
    /// world. Refused if that would make a loop.
    bool reparent(Entity child, Entity newParent);
    /// Puts the entity just before or just after `sibling`, under the same parent as it:
    /// what dropping a row between two rows of the Hierarchy does.
    bool moveBeside(Entity entity, Entity sibling, bool after);
    /// Sets where an entity is in the world, whatever its parent.
    void setWorldMatrix(Entity entity, const Mat4& world);

    std::shared_ptr<Material> defaultMaterial();
    int materialIndex(const std::shared_ptr<Material>& material) const;

    // ---- undo

    /// Call when something in the scene was edited. The step is recorded by endFrame() once
    /// `interacting` is false (the drag is over, the text field is left).
    void markChanged() { touched_ = true; }
    /// The scene read again from itself: copies of prefabs made again from their prefabs (with
    /// their own changes). One step of undo.
    void rebuild();
    /// A part of a prefab's copy was moved: where it is now is kept as the copy's own change.
    void keepPartPlace(Entity part);
    void undo();
    void redo();
    bool canUndo() const { return !undo_.empty() && !isPlaying_; }
    bool canRedo() const { return !redo_.empty() && !isPlaying_; }

    // ---- play

    bool isPlaying() const { return isPlaying_; }
    bool isPaused() const { return isPlaying_ && engine_.isSimulationPaused; }
    void setPlaying(bool playing);
    void setPaused(bool paused);
    /// While paused: lets the game run for one frame, then pauses again.
    void stepFrame();
    /// The camera the game is seen through: the scene's own active one. Invalid if it has none.
    Entity gameCamera() const;

    /// Puts the selected entities where the view's camera is, facing the way it faces
    /// (the way to aim a camera or a light: look, then align).
    void alignSelectionWithView();
    /// The reverse: the view goes to where the selected entity is and looks its way.
    void alignViewWithSelection();
    /// Runs `swap`, which replaces the game module, with the scene safely out of the way.
    void reloadGameCode(const std::function<void()>& swap);

    // ---- the editor's camera. It is an entity of the scene, never saved.

    Entity cameraEntity() const { return camera_; }
    Vec3 cameraPosition{6, 5, 9};
    float cameraYaw = 0.6f, cameraPitch = -0.42f;
    float orbitDistance = 10.0f;                // how far in front of the camera its pivot is
    Quat cameraRotation() const { return eulerAngles(cameraPitch, cameraYaw); }
    Vec3 cameraForward() const { return rotate(cameraRotation(), Vec3{0, 0, -1}); }
    /// Moves the camera so that the selection fills the view.
    void frameSelection();

    // ---- once per frame

    void beginFrame();
    void endFrame(bool interacting);
    /// A collider that fits its mesh goes on fitting it while the entity is scaled, by the
    /// handles or in the Inspector. One that was sized by hand is left as it is.
    void keepCollidersFitted();

private:
    static void populateDefaultScene(Scene& scene, Assets& assets, std::vector<NamedMaterial>& materials);
    void restore(const std::string& text);
    void createCamera();
    void syncCamera();
    std::string snapshot();
    bool stepIsRunning_ = false;
    void resetHistory();
    void copyComponents(Entity from, Entity to);
    Entity duplicateTree(Entity entity, Entity newParent);

    Engine& engine_;
    std::vector<Entity> entities_;
    std::vector<Entity> selection_;
    Entity camera_;

    std::vector<std::string> undo_, redo_;
    std::string current_, saved_;
    struct ScaleSeen {
        Entity entity;
        Vec3 scale;
    };
    std::vector<ScaleSeen> scaleSeen_;      // the selected entities' scales as of the last frame
    bool touched_ = false;

    bool isPlaying_ = false;
    std::string beforePlay_;
};

}  // namespace molten::editor
