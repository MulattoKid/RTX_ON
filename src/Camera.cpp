/*
Copyright (c) 2018-2019 Daniel Fedai Larsen
LICENSE: See end of file for license information.
*/

#include "Camera.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/geometric.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/trigonometric.hpp"

Camera::Camera(const unsigned int filmWidth, const unsigned int filmHeight, const float verticalFOV, const glm::vec3& origin, const glm::vec3& viewDir)
{
	this->filmWidth = filmWidth;
	this->filmHeight = filmHeight;
	this->verticalFOV = glm::radians(verticalFOV);
	this->aspectRatio = filmWidth / float(filmHeight);
	this->origin = origin;
	this->viewDir = viewDir;
	
	float lensHalfHeight = glm::tan(this->verticalFOV / 2.0f);
	float lensHalfWidth = lensHalfHeight * this->aspectRatio;
	float lensWidth = lensHalfWidth * 2.0f;
	float lensHeight = lensHalfHeight * 2.0f;
	
	//Calculate the three vectors that define the camera	
	static const glm::vec3 baseUp(0.0f, 1.0f, 0.0f);
	glm::vec3 right = glm::normalize(glm::cross(viewDir, baseUp));
	up = glm::normalize(glm::cross(right, viewDir));
	//Calculate top_left_corner
	//1) Start at camera's position
	//2) Go lens_half_width along the camera's left (-right) axis
	//3) Go lens_half_height along the camera's up axis
	//4) Go 1 unit along the camera's view_direction axis
	topLeftCorner = this->origin + (lensHalfWidth * (-right)) + (lensHalfHeight * up) + viewDir;
	//Go width of lense along the camera's right axis
	horizontalEnd = lensWidth * right;
	//Go height of lense along the camera's down (-up) axis
	verticalEnd = lensHeight * (-up);
}

// https://glm.g-truc.net/0.9.4/api/a00151.html
glm::mat4x4 Camera::GetViewProjectionMatrix()
{
	glm::mat4x4 view = glm::lookAt(origin, origin + viewDir, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4x4 projection = glm::perspective(verticalFOV, aspectRatio, 0.01f, 100.0f);
	return projection * view;
}

void Camera::Update()
{
	float lensHalfHeight = glm::tan(this->verticalFOV / 2.0f);
	float lensHalfWidth = lensHalfHeight * this->aspectRatio;
	float lensWidth = lensHalfWidth * 2.0f;
	float lensHeight = lensHalfHeight * 2.0f;
	
	//Calculate the three vectors that define the camera	
	static const glm::vec3 baseUp(0.0f, 1.0f, 0.0f);
	glm::vec3 right = glm::normalize(glm::cross(viewDir, baseUp));
	glm::vec3 up = glm::normalize(glm::cross(right, viewDir));
	//Calculate top_left_corner
	//1) Start at camera's position
	//2) Go lens_half_width along the camera's left (-right) axis
	//3) Go lens_half_height along the camera's up axis
	//4) Go 1 unit along the camera's view_direction axis
	topLeftCorner = this->origin + (lensHalfWidth * (-right)) + (lensHalfHeight * up) + viewDir;
	//Go width of lense along the camera's right axis
	horizontalEnd = lensWidth * right;
	//Go height of lense along the camera's down (-up) axis
	verticalEnd = lensHeight * (-up);
}

/*
MIT License

Copyright (c) 2018-2019 Daniel Fedai Larsen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
