#pragma once
#include "draw_item.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat4x4.hpp>
DISABLE_WARNINGS_POP()
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A node in the transformation hierarchy.
//
//     world = parent.world * rest * anim
//
// "rest" places the node relative to its parent once (e.g. where the mast is stepped on the hull),
// "anim" is rewritten every frame by the animation code (e.g. the boom swinging about the mast).
// Splitting the two keeps the animation code free of the layout numbers.
class SceneNode {
public:
    explicit SceneNode(std::string name, GPUMesh* mesh = nullptr, const PBRMaterial* material = nullptr, const glm::mat4& rest = glm::mat4(1.0f));

    // Adds a child and returns a reference to it, so trees can be built in a few lines.
    SceneNode& addChild(std::string name, GPUMesh* mesh = nullptr, const PBRMaterial* material = nullptr, const glm::mat4& rest = glm::mat4(1.0f));

    // Recompute world matrices of this node and its whole subtree.
    void updateWorld(const glm::mat4& parentWorld = glm::mat4(1.0f));
    // Append every visible node with a mesh to the draw list.
    void collect(std::vector<DrawItem>& out) const;
    // Depth-first search by name, nullptr if not found.
    SceneNode* find(std::string_view nodeName);

    std::string name;
    GPUMesh* mesh;
    const PBRMaterial* material;
    glm::mat4 rest;
    glm::mat4 anim { 1.0f };
    glm::mat4 world { 1.0f };
    bool visible { true };
    std::vector<std::unique_ptr<SceneNode>> children;
};
