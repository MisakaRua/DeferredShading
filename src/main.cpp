#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <cassert>
#include <unordered_map>
#include <random>
#include <chrono>
#include <cmath>
#include <memory>
#include <cmath>

#include <glad/glad.h>
#include <glfw/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <stb/stb_image_write.h>


#include "Timer.h"
#include "Shader.h"
#include "Camera.h"
#include "Sphere.h"
#include "Plane.h"
#include "GBuffer.h"
#include "Texture.h"
#include "PointLight.h"
#include "HdrTexture.h"
#include "Cube.h"
#include "PrecomputeBRDF.h"


uint32_t kScreenWidth = 1440;
uint32_t kScreenHeight = 810;
const char* kWndName = "Deferred Shading";


int main(int argc, char** argv)
{
	// glfw initialize
	assert(glfwInit());
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

	GLFWwindow* window = glfwCreateWindow(kScreenWidth, kScreenHeight, kWndName, nullptr, nullptr);
	assert(window);
	glfwMakeContextCurrent(window);

	assert(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
	glViewport(0, 0, kScreenWidth, kScreenHeight);


	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);


	// imgui settings
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	ImFontConfig font;
	font.SizePixels = 20.0f;
	io.Fonts->AddFontDefault(&font);

	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 430");


	// grid style sphere generation
	static constexpr size_t xCount = 1;
	static constexpr size_t yCount = 1;
	Sphere<64, 64> spheres[xCount * yCount];
	glm::vec3 positions[xCount * yCount];
	for (size_t x = 0; x < xCount; ++x)
	{
		for (size_t y = 0; y < yCount; ++y)
		{
			positions[x * yCount + y] = { (x * 2.0f - xCount + 1.0f) * 1.5f, (y * 2.0f - yCount + 1.0f) * 1.5f, 0.0f };
		}
	}


	// for deferred shading, like post-process
	Plane showBoard;


	// camera is set at a unchangable distance, while the direction to the objs can be change.
	static constexpr float cameraDistance = yCount * 4.0f;
	auto getPos = [](float theta, float phi) -> glm::vec3
	{
		theta = glm::radians(theta); // angle to y axis
		phi = glm::radians(phi); // angle project point to z axis
		glm::vec3 pos;
		pos.x = glm::sin(theta) * glm::sin(phi);
		pos.y = glm::cos(theta);
		pos.z = glm::sin(theta) * glm::cos(phi);

		return pos * cameraDistance;
	};

	float cameraTheta = 90.0f;
	float cameraPhi = 0.0f;
	Camera camera(getPos(cameraTheta, cameraPhi), { 0.0f, 0.0f, 0.0f });


	// 4 lights for default setting. 
	PointLight lights[] =
	{
		{ { -10.0f, +10.0f, 10.0f }, { 300.0f, 300.0f, 300.0f } },
		{ { +10.0f, +10.0f, 10.0f }, { 300.0f, 300.0f, 300.0f } },
		{ { -10.0f, -10.0f, 10.0f }, { 300.0f, 300.0f, 300.0f } },
		{ { +10.0f, +10.0f, 10.0f }, { 300.0f, 300.0f, 300.0f } },
	};


	Cube skybox;
	Plane debugPlane;


	Shader shaderTest("shader/debug.vert", "shader/debug.frag"); // debug shader
	Shader shaderGBuf("shader/gBuffer.vert", "shader/gBuffer.frag"); // generate g-buf
	Shader shaderPBR("shader/pbr.vert", "shader/pbr.frag");	// use g-buf to render objs, using microfacet model
	Shader shaderSkybox("shader/skybox.vert", "shader/skybox.frag"); // skybox


	GBuffer gBuffer;

	Texture baseColor("res/rustediron/rustediron2_basecolor.png");
	Texture normalMap("res/rustediron/rustediron2_normal.png");
	Texture metallic("res/rustediron/rustediron2_metallic.png");
	Texture roughness("res/rustediron/rustediron2_roughness.png");

	HdrTexture hdr("res/Newport_Loft/Newport_Loft_Ref.hdr");


	PrecomputerBRDF pBrdf;


	glViewport(0, 0, kScreenWidth, kScreenHeight);
	while (!glfwWindowShouldClose(window))
	{
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

		// update
		{
			camera.pos = getPos(cameraTheta, cameraPhi);
			camera.Update({ 0.0f, 0.0f, 0.0f });
		}


		// generate gBuffer
		// enable g-buf
		gBuffer.begin();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		{
			shaderGBuf.bind();
			shaderGBuf.setValue("proj", camera.getProj());
			shaderGBuf.setValue("view", camera.getView());

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, baseColor.texture);
			shaderGBuf.setValue("BaseColor", 0);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, normalMap.texture);
			shaderGBuf.setValue("NormalMap", 1);

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, metallic.texture);
			shaderGBuf.setValue("Metallic", 2);

			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, roughness.texture);
			shaderGBuf.setValue("Roughness", 3);

			for (size_t i = 0; i < xCount * yCount; ++i)
			{
				glm::mat4 model = glm::translate(glm::mat4(1.0f), positions[i]);
				shaderGBuf.setValue("model", model);

				glBindVertexArray(spheres[i].vao);
				glDrawElements(GL_TRIANGLE_STRIP, spheres[i].data.indices.size(), GL_UNSIGNED_INT, nullptr);
			}
		}
		gBuffer.end();


		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		{
			shaderPBR.bind();

			shaderPBR.setValue("cameraPos", camera.pos);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, gBuffer.gPosition);
			shaderPBR.setValue("gPosition", 0);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, gBuffer.gBasecolor);
			shaderPBR.setValue("gBaseColor", 1);

			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, gBuffer.gNormal);
			shaderPBR.setValue("gNormal", 2);

			glActiveTexture(GL_TEXTURE3);
			glBindTexture(GL_TEXTURE_2D, gBuffer.gMetallicRoughness);
			shaderPBR.setValue("gMetallicRoughness", 3);

			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_CUBE_MAP, hdr.prefilterMap);
			shaderPBR.setValue("PrefilterMap", 4);

			glActiveTexture(GL_TEXTURE5);
			glBindTexture(GL_TEXTURE_2D, pBrdf.brdfLUT);
			shaderPBR.setValue("BrdfLut", 5);


			for (int i = 0; const auto & light : lights)
			{
				shaderPBR.setValue("lights[" + std::to_string(i) + "].position", light.position);
				shaderPBR.setValue("lights[" + std::to_string(i) + "].intensity", light.intensity);

				++i;
			}

			glBindVertexArray(showBoard.vao);
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
		}

		// skybox
		{
			glDisable(GL_DEPTH_TEST);
			shaderSkybox.bind();

			shaderSkybox.setValue("proj", camera.getProj());
			shaderSkybox.setValue("view", camera.getView());

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_CUBE_MAP, hdr.hdrCubeMap);
			shaderSkybox.setValue("SkyBox", 0);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, gBuffer.gDepthMap);
			shaderSkybox.setValue("DepthMap", 1);

			shaderSkybox.setValue("wndWidth", static_cast<float>(kScreenWidth));
			shaderSkybox.setValue("wndHeight", static_cast<float>(kScreenHeight));

			glBindVertexArray(skybox.vao);
			glDrawArrays(GL_TRIANGLES, 0, 36);
			glEnable(GL_DEPTH_TEST);
		}

		//// debug
		//{
		//  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		//	shaderTest.bind();

		//	glActiveTexture(GL_TEXTURE0);
		//	glBindTexture(GL_TEXTURE_2D, pBrdf.brdfLUT);
		//	shaderTest.setValue("Framebuffer", 0);

		//	glBindVertexArray(debugPlane.vao);
		//	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
		//}

		{
			// Start the Dear ImGui frame
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();


			ImGui::Begin("PBR rendering");
			{
				if (ImGui::TreeNode("Camera Position"))
				{
					ImGui::SliderFloat("Theta", &cameraTheta, 20.0f, 160.0f);
					ImGui::SliderFloat("Phi", &cameraPhi, -180.0f, 180.0f);

					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Utils"))
				{
					if (ImGui::Button("Screen Shot"))
					{
						std::vector<uint8_t> pixels(4 * kScreenHeight * kScreenWidth);

						glReadPixels(0, 0, kScreenWidth, kScreenHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
						
						stbi_flip_vertically_on_write(true);
						stbi_write_png("res/ScreenShot.png", kScreenWidth, kScreenHeight, 4, pixels.data(), 0);
					}

					ImGui::TreePop();
				}
			}
			ImGui::End();


			// Rendering
			ImGui::Render();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		}


		glfwSwapBuffers(window);
		glfwPollEvents();
	}


	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();


	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}