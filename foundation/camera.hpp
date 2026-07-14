#pragma once
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


class Camera
{
public:
    Camera();

    glm::mat4 getViewMatrix() const;
    void updateViewMatrix();

    glm::mat4 getProjMatrix() const;
    void updateProjMatrix();

    // Renamed: avoids shadowing glm::lookAt
    void setPosition(glm::vec3 pos);
    void setDirection(glm::vec3 dir);
    void setWorldUp(glm::vec3 worldUp);

    void changeLens(float FOV, float nearP = -1.0f, float farP = -1.0f);
    void updateAspectRatio(float aspectRatio);

    void serialize(std::ofstream &wf) const;
    void deserialize(std::ifstream &wf);

    void getFrustumCornersWorldSpace(
        float nearPlane,
        float farPlane,
    
    glm::vec3 corners[8]) const;
    glm::vec3 pos{};
    glm::vec3 dir{};
    glm::vec3 worldUp{};
    glm::vec3 right{};

    float FOV{};
    float nearPlane{};
    float farPlane{};
    float aspectRatio{};

    bool hasChangedView = false;
    bool hasChangedProj = false;

private:
    glm::mat4 viewMatrix{};
    glm::mat4 projMatrix{};
};