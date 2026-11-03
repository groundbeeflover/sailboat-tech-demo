#include "scene_node.h"
#include <utility>

SceneNode::SceneNode(std::string name_, GPUMesh* mesh_, const PBRMaterial* material_, const glm::mat4& rest_)
    : name(std::move(name_))
    , mesh(mesh_)
    , material(material_)
    , rest(rest_)
{
}

SceneNode& SceneNode::addChild(std::string childName, GPUMesh* childMesh, const PBRMaterial* childMaterial, const glm::mat4& childRest)
{
    children.push_back(std::make_unique<SceneNode>(std::move(childName), childMesh, childMaterial, childRest));
    return *children.back();
}

void SceneNode::updateWorld(const glm::mat4& parentWorld)
{
    world = parentWorld * rest * anim;
    for (auto& child : children)
        child->updateWorld(world);
}

void SceneNode::collect(std::vector<DrawItem>& out) const
{
    if (!visible)
        return; // hides the whole subtree
    if (mesh && material)
        out.push_back(DrawItem { mesh, material, world });
    for (const auto& child : children)
        child->collect(out);
}

SceneNode* SceneNode::find(std::string_view nodeName)
{
    if (name == nodeName)
        return this;
    for (auto& child : children) {
        if (SceneNode* found = child->find(nodeName))
            return found;
    }
    return nullptr;
}
