#include "camera.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

Camera::Camera()
{
    this->setPosition(glm::vec3(0));
    this->setDirection(glm::vec3(0, 0, -1));
    this->setWorldUp(glm::vec3(0, 1, 0));
    this->changeLens(100.f, 0.1f, 100000.f);
}

glm::mat4 Camera::getViewMatrix() const
{
    return this->viewMatrix;
}

void Camera::updateViewMatrix()
{
    this->viewMatrix = glm::lookAt(this->pos, this->pos + this->dir, this->worldUp);
    hasChangedView = false;
}

glm::mat4 Camera::getProjMatrix() const
{
    return this->projMatrix;
}

void Camera::updateProjMatrix()
{
    this->projMatrix = glm::perspective(glm::radians(FOV), aspectRatio, nearPlane, farPlane);
    this->projMatrix[1][1] *= -1.0f;
    hasChangedProj = false;
}

void Camera::setPosition(glm::vec3 pos)
{
    this->pos = pos;
    hasChangedView = true;
}

void Camera::setDirection(glm::vec3 dir)
{
    this->dir = dir;
    this->right = glm::cross(dir, worldUp);
    hasChangedView = true;
}

void Camera::setWorldUp(glm::vec3 worldUp)
{
    this->worldUp = worldUp;
    this->right = glm::cross(dir, worldUp);
    hasChangedView = true;
}

void Camera::changeLens(float FOV, float nearP, float farP)
{
    this->FOV = FOV;
    if (nearP > 0)
        nearPlane = nearP;
    if (farP > 0)
        farPlane = farP;
    hasChangedProj = true;
}

void Camera::updateAspectRatio(float aspectRatio)
{
    this->aspectRatio = aspectRatio;
    hasChangedProj = true;
}

void Camera::serialize(std::ofstream &wf) const
{
    wf.write((char *)&pos, sizeof(pos));
    wf.write((char *)&dir, sizeof(dir));
    wf.write((char *)&worldUp, sizeof(worldUp));
    wf.write((char *)&FOV, sizeof(FOV));
    wf.write((char *)&nearPlane, sizeof(nearPlane));
    wf.write((char *)&farPlane, sizeof(farPlane));
}

void Camera::deserialize(std::ifstream &wf)
{
    wf.read((char *)&pos, sizeof(pos));
    wf.read((char *)&dir, sizeof(dir));
    wf.read((char *)&worldUp, sizeof(worldUp));
    wf.read((char *)&FOV, sizeof(FOV));
    wf.read((char *)&nearPlane, sizeof(nearPlane));
    wf.read((char *)&farPlane, sizeof(farPlane));
    this->right = glm::cross(dir, worldUp);
    hasChangedProj = true;
    hasChangedView = true;
}

void Camera::getFrustumCornersWorldSpace(
    float nearPlane,
    float farPlane,
    glm::vec3 corners[8]) const
{
    glm::mat4 proj = glm::perspective(
        glm::radians(FOV),
        aspectRatio,
        nearPlane,
        farPlane);

    // Vulkan clip space has an inverted Y axis.
    proj[1][1] *= -1.0f;

    glm::mat4 inv = glm::inverse(proj * getViewMatrix());

    int index = 0;

    for (int z = 0; z < 2; ++z)
    {
        for (int y = 0; y < 2; ++y)
        {
            for (int x = 0; x < 2; ++x)
            {
                glm::vec4 pt = inv * glm::vec4(
                    x ? 1.0f : -1.0f,
                    y ? 1.0f : -1.0f,
                    z ? 1.0f : -1.0f,
                    1.0f);

                corners[index++] = glm::vec3(pt) / pt.w;
            }
        }
    }
}