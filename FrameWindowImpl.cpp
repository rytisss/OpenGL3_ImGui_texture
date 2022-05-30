//This code is create by Rytis Augustauskas and it is distributed in https://github.com/rytisss/OpenGL3_ImGui_texture

#include "frameWindowImpl.h"
#include <iostream>
#include <algorithm>
#include "ControlGraphics.h"
#include "Timer.h"
#include "CircleGraphics.h"
#include "json.hpp"
#include "LogMessenger.h"

#define DEFAULTCONTEXTWIDTH 130.f

#define DEFAULTINFOTEXTCOLOR_R 0.64f
#define DEFAULTINFOTEXTCOLOR_G 0.46f
#define DEFAULTINFOTEXTCOLOR_B 0.71f
#define DEFAULTINFOTEXTCOLOR_A 1.f

#define DEFAULTCANVASCOLOR_R 0.1f
#define DEFAULTCANVASCOLOR_G 0.1f
#define DEFAULTCANVASCOLOR_B 0.1f
#define DEFAULTCANVASCOLOR_A 1.f

#define MAGICGLOWNUMBER 159.15494f

#define MINWINDOWSIZE 300.f


FrameWindow::FrameWindowImpl::FrameWindowImpl(FrameWindow& parent)
	: m_parent(parent)
	, m_initialized(false)
	, m_firstTime(true)
	, m_latestImageWidth(1920)
	, m_latestImageHeight(1080)
	, m_latestResizedImageWidth(1920)
	, m_latestResizedImageHeight(1080)
	, m_checkGeometriesChange(false)
	, m_leftOrtho(0.f)
	, m_rightOrtho(0.f)
	, m_topOrtho(0.f)
	, m_bottomOrtho(0.f)
	, m_windowSetDuration(0.f)
	, m_lastFrameCaptureTime(-1)
	, m_frameBuffer(-1)
	, m_title("Frame Window")
	, m_windowID(-1)
	, m_panX(0.f)
	, m_panY(0.f)
	, m_zoom(1.1f)
	, m_zoom_factor(0.05f)
	, m_pan_factor(0.01f)
	, m_mouseDownX(0.0f)
	, m_mouseDownY(0.0f)
	, m_mouseDownCtrlX(0.f)
	, m_mouseDownCtrlY(0.f)
	, m_panOnMouseDownX(0.0f)
	, m_panOnMouseDownY(0.0f)
	, m_canvasFocused(false)
	, m_wasMouseClickedInsideCanvas(false)
	, m_canvasColor(DEFAULTCANVASCOLOR_R, DEFAULTCANVASCOLOR_G, DEFAULTCANVASCOLOR_B, DEFAULTCANVASCOLOR_A)
	, m_infoTextColor(DEFAULTINFOTEXTCOLOR_R, DEFAULTINFOTEXTCOLOR_G, DEFAULTINFOTEXTCOLOR_B, DEFAULTINFOTEXTCOLOR_A)
	, m_showContextColumn(true)
	, m_contextColumnWidthSet(false)
	, m_canvasXOffset(0)
	, m_mouseXInCanvas(0.f)
	, m_mouseYInCanvas(0.f)
	, m_mouseXInTexture(0.f)
	, m_mouseYInTexture(0.f)
	, m_geometriesIDCount(-1)
	, m_internalElementAddEnabled(true)
	, m_latestPixelRatio(1.f)
	, m_showWindowID(false)
	, m_resizeRatio(1.f)
	, m_resizeRatio_(1.f)
{
	m_latestAspectRatio = float(m_latestImageWidth) / float(m_latestImageHeight);
	ResetCanvasParameters();
}

void FrameWindow::FrameWindowImpl::Init(int id)
{
	if (!m_initialized)
	{
		m_windowID = id;
		glGenTextures(m_windowID, &m_frameBuffer);
		// Define the viewport dimensions
		glBindTexture(GL_TEXTURE_2D, m_frameBuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		float borderColor[] = { m_canvasColor.x, m_canvasColor.y, m_canvasColor.z, 1.0f };
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
		m_initialized = true;
		UploadInitialImage();
		m_frame.captureTime = 1;
	}
}

void FrameWindow::FrameWindowImpl::Render()
{
	//try to initialize, do nothing
	if (!m_initialized) { return; }
	if (ImGui::GetIO().DisplaySize.y > 0)
	{
		ImGui::PushStyleColor(ImGuiCol_ResizeGrip, 0);
		std::string window_name = m_title;
		if (m_showWindowID)
		{
			window_name += ", ID" + std::to_string(m_windowID);
		}
		static ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;
		ImGui::SetNextWindowSizeConstraints(ImVec2(MINWINDOWSIZE, MINWINDOWSIZE),
			ImGui::GetIO().DisplaySize);
		if (ImGui::Begin(window_name.c_str(), NULL, windowFlags))
		{
			if (m_firstTime)
			{
				m_firstTime = false;
				AdjustWindowSize();
				ImGui::End();
				ImGui::PopStyleColor();
				return;
			}
			std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
			m_windowPos = ImGui::GetWindowPos();
			m_windowSize = ImGui::GetWindowSize();

			UpdateTexture();

			if (m_firstTime)
			{
				ImGui::End();
				ImGui::PopStyleColor();
				m_firstTime = false;
				return;
			}

			//calculate canvas postion in window
			SetCanvasPosition();

			GLsizei left = 0;
			GLsizei top = 0;
			GLsizei right = static_cast<GLsizei>(m_canvasWidth);
			GLsizei bottom = static_cast<GLsizei>(m_canvasHeight);

			//calculate pixel ratio
			//it should be equal along width and height
			if (m_latestImageWidth > 0)
			{
				m_latestPixelRatio = m_canvasWidth > 0 ? m_canvasWidth / (float)m_latestImageWidth / m_zoom : 1.f;
			}
			else
			{
				m_latestPixelRatio = 1.f;
			}

			//find aspect ratio
			int viewportWidth = right;
			int viewportHeight = bottom;
			if (viewportWidth <= 0) viewportWidth = 1;
			if (viewportHeight <= 0) viewportHeight = 1;

			float viewportAspectRatio = float(viewportWidth) / float(viewportHeight);
			
			//https://stackoverflow.com/questions/35810782/opengl-view-projections-and-orthographic-aspect-ratio
			float widthReduction = 1.f;
			float heightReduction = 1.f;
			if (viewportAspectRatio >= m_latestAspectRatio) {
				widthReduction = viewportAspectRatio / m_latestAspectRatio;
			}
			else 
			{
				heightReduction = m_latestAspectRatio / viewportAspectRatio;
			}
			
			glViewport(left, top, right, bottom);
			glClearColor(0.2f, 0.2f, 0.3f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);  
			glBindFramebuffer(GL_FRAMEBUFFER, 0); 

			m_textureWidthOffset = (widthReduction - 1.f) / 2.f;
			m_textureHeightOffset = (heightReduction - 1.f) / 2.f;

			//make converter
			//form canvas to texture coordinates converter
			m_canvas2TextureConverter = std::bind(&FrameWindow::FrameWindowImpl::Texture2CanvasCoord,
				this,
				std::placeholders::_1,
				std::placeholders::_2,
				std::placeholders::_3,
				std::placeholders::_4);

			//check if window is active
			HandleMouseManipulations();

			//calculate texture in canvas position
			CalculateQuad();
			
			//calculate few intenal parameters
			UpdateConverter();

			CalculateMouseCoordinateOnTexture();

			//add only if texture fits to the screen
			if (ImGui::GetIO().DisplaySize.x >= m_canvasTopLeft.x)
			{
				ImGui::PushID((void*)(intptr_t)m_frameBuffer);
				ImGui::GetWindowDrawList()->AddImageQuad(
					(ImTextureID)(m_frameBuffer),
					m_canvasTopLeft,
					m_canvasTopRight,
					m_canvasBottomRight,
					m_canvasBottomLeft,
					m_uv_0,
					m_uv_1,
					m_uv_2,
					m_uv_3,
					IM_COL32_WHITE);
				ImGui::PopID();

				DrawGraphicalElements();
			}
			DrawFrameOnFocus();

			std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
			m_windowSetDuration = (float)duration / 1000.f;
		}
		ImVec2 contextSize = ImGui::GetContentRegionAvail();
		m_parent.m_contextWidth = contextSize.x;
		m_parent.m_contextHeight = contextSize.y;
		ImGui::End();
		ImGui::PopStyleColor();
	}

	//copy graphical object if we want to inspect 
	if (m_checkGeometriesChange)
	{
		std::vector<int> changeGeometriesIndeces = GetChangedGraphicalObjectIndeces();
		if (changeGeometriesIndeces.size() > 0)
		{
			m_parent.OnGeometryObjectChange(changeGeometriesIndeces);
		}
		CopyCurrentGeometriesToOld();
	}
}

void FrameWindow::FrameWindowImpl::DeInit()
{
	if (m_initialized)
	{
		// Properly deallocate all resources once they've outlived their purpose
		glDeleteFramebuffers((intptr_t)m_frameBuffer, &m_frameBuffer);
		m_initialized = false;
	}
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		delete m_graphicalObjects[i];
	}
	m_graphicalObjects.clear();
}

void FrameWindow::FrameWindowImpl::AddGraphicalObject(GraphicalObject* pGraphicalObject, bool configLoad)
{
	std::unique_lock<std::mutex> lock(m_lock);
	if (pGraphicalObject == nullptr)
	{
		LogMessenger::Error("Graphical object is nullptr!");
		return;
	}
	//Graphical object should be initialized before adding
	if (!pGraphicalObject->IsInitialized())
	{
		LogMessenger::Error("Graphical object is not initialized, it will be initialized!");
		pGraphicalObject->Init(GetUniqueId());
		m_graphicalObjects.push_back(pGraphicalObject);
	}
	else
	{
		if (configLoad) // exception in loading from config
		{
			m_graphicalObjects.push_back(pGraphicalObject);
		}
		else
		{
			LogMessenger::Warning("Graphical object is already initialized, can't add it!");
		}
	}
}

void FrameWindow::FrameWindowImpl::DeleteGraphicalObject(GraphicalObject* pGraphicalObject)
{
	std::unique_lock<std::mutex> lock(m_lock);
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		if (pGraphicalObject == m_graphicalObjects[i])
		{
			LogMessenger::Info("Deleting graphical object with id " + std::to_string(m_graphicalObjects[i]->GetID()));
			m_graphicalObjects[i]->DeInit();
			delete m_graphicalObjects[i];
			m_graphicalObjects[i] = nullptr;
		}
	}
	//take out empty graphical objects
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		if (m_graphicalObjects[i] == nullptr)
		{
			m_graphicalObjects.erase(m_graphicalObjects.begin() + i);
			i--;
		}
	}
}

void FrameWindow::FrameWindowImpl::DeleteGraphicalObject(int id)
{
	std::unique_lock<std::mutex> lock(m_lock);
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		if (id == m_graphicalObjects[i]->GetID())
		{
			LogMessenger::Info("Deleting grapgical object with id " + std::to_string(m_graphicalObjects[i]->GetID()));
			m_graphicalObjects[i]->DeInit();
			delete m_graphicalObjects[i];
			m_graphicalObjects[i] = nullptr;
		}
	}
	//take out empty graphical objects
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		if (m_graphicalObjects[i] == nullptr)
		{
			m_graphicalObjects.erase(m_graphicalObjects.begin() + i);
			i--;
		}
	}
}

std::vector<GraphicalObject*> FrameWindow::FrameWindowImpl::GetGraphicalObjects()
{
	return m_graphicalObjects;
}

void FrameWindow::FrameWindowImpl::SetName(std::string title)
{
	std::unique_lock<std::mutex> lock(m_lock);
	m_title = title;
}

void FrameWindow::FrameWindowImpl::SetAutoResize(bool state)
{
}

int FrameWindow::FrameWindowImpl::GetWindowWidth()
{
	return 0;
}

int FrameWindow::FrameWindowImpl::GetWindowHeight()
{
	return 0;
}

std::vector<cv::Rect> FrameWindow::FrameWindowImpl::GetRectangles()
{
	std::unique_lock<std::mutex> lock(m_lock);
	//Get rectangle regions from GUI
	std::vector<cv::Rect> regions;
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		GraphicalObject* element = m_graphicalObjects[i];
		//try casting to rectangle
		RectangularGraphics* pRectGraphics = dynamic_cast<RectangularGraphics*>(element);
		if (pRectGraphics != nullptr)
		{
			cv::Rect rect = cv::Rect(pRectGraphics->x, pRectGraphics->y, pRectGraphics->width, pRectGraphics->height);
			regions.push_back(rect);
		}
	}
	return regions;
}

bool FrameWindow::FrameWindowImpl::IsWindowMovementDisabled()
{
	return m_canvasFocused;
}

void FrameWindow::FrameWindowImpl::EnableControlElementsAdd(bool status)
{
	m_internalElementAddEnabled = status;
}

void FrameWindow::FrameWindowImpl::EnableGeometriesChangeNotification(bool status)
{
	m_checkGeometriesChange = status;
}

bool FrameWindow::FrameWindowImpl::IsControlElementsAddEnabled()
{
	return m_internalElementAddEnabled;
}

void FrameWindow::FrameWindowImpl::ShowWindowIDInTitle(bool status)
{
	m_showWindowID = status;
}

cv::Size FrameWindow::FrameWindowImpl::GetCurrentTextureSize()
{
	std::unique_lock<std::mutex> lock(m_lock);
	return cv::Size(m_latestImageWidth, m_latestImageHeight);
}

void FrameWindow::FrameWindowImpl::GetPanZoom(float& panX, float& panY, float& zoom)
{
	panX = m_panX;
	panY = m_panY;
	zoom = m_zoom;
}

void FrameWindow::FrameWindowImpl::SetPanZoom(float panX, float panY, float zoom)
{
	m_panX = panX;
	m_panY = panY;
	m_zoom = zoom;
}

std::string FrameWindow::FrameWindowImpl::GetConfig()
{
	//make nested configuration of all graphical control elements
	nlohmann::json graphicalElementsConfig;
	int graphicalElementCounter = 0;
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		std::string graphicalElementConfig = m_graphicalObjects[i]->GetConfig();
		if (graphicalElementConfig == "")
		{
			continue;
		}
		//append json with nested grapgical control json
		std::string index = std::to_string(graphicalElementCounter);
		graphicalElementsConfig[index] = nlohmann::json::parse(graphicalElementConfig);
		graphicalElementCounter++;
	}
	std::string graphicalElementsConfig_str = graphicalElementsConfig.dump();
	return graphicalElementsConfig_str;
}

void FrameWindow::FrameWindowImpl::SetConfig(std::string& config)
{
	//try to parse and load config
	try
	{
		nlohmann::json graphicalElementsConfig = nlohmann::json::parse(config);
		for (auto& graphicalElement : graphicalElementsConfig.items())
		{
			
			LogMessenger::Trace("Loading graphical elements " + graphicalElement.key());
			// deserialize, get type and recreate object
			nlohmann::json graphicalElementConfig = nlohmann::json::parse(graphicalElement.value().dump());

			GraphicalObject* graphicalElement = nullptr;
			if (graphicalElementConfig["type"] == "CircleGraphics")
			{
				graphicalElement = new CircleGraphics();
				graphicalElement->SetConfig(graphicalElementConfig.dump());
			}
			else if (graphicalElementConfig["type"] == "RectangleGraphics")
			{
				graphicalElement = new RectangularGraphics();
				graphicalElement->SetConfig(graphicalElementConfig.dump());
			}
			else if (graphicalElementConfig["type"] == "PolygonGraphics")
			{
				graphicalElement = new PolygonGraphics();
				graphicalElement->SetConfig(graphicalElementConfig.dump());
			}

			if (graphicalElement != nullptr)
			{
				this->AddGraphicalObject(graphicalElement, true);
				//increment 'unique id' if added object has bigger id
				if (graphicalElement->GetID() > m_geometriesIDCount)
				{
					m_geometriesIDCount = graphicalElement->GetID();
				}
			}
		}
	}
	catch (...)
	{
		LogMessenger::Error("Failed to load/parse configuration in " + this->m_title);
	}
}

void FrameWindow::FrameWindowImpl::UpdateFrame(Frame& frame)
{
	std::unique_lock<std::mutex> lock(m_lock);
	//update only if captureTime/timeStamp is different
	if (frame.image.empty()) return;
	m_frame.index = frame.index;
	m_frame.captureTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();//frame.captureTime;
	float heightResizeFactor = this->m_canvasHeight / (float)frame.image.rows;
	float widthResizeFactor = this->m_canvasWidth / (float)frame.image.cols;

	//std::log2f() TODO used this

	m_resizeRatio = std::min(heightResizeFactor, widthResizeFactor) / this->m_zoom;
	//std::cout << "Resize factor " << m_frameResizeFactor << std::endl;
	if (m_resizeRatio <= 0.f || m_resizeRatio >= 1.f)
	{
		LogMessenger::Warning("FrameWindow: Not optimizing frame!");
		//can happen while loading gui
		m_resizeRatio = 1.f;
		Image2RGBA(frame.image, m_frame.image);
	}
	else
	{
		cv::Mat temp;
		cv::resize(frame.image, temp, cv::Size(0, 0), m_resizeRatio, m_resizeRatio);
		Image2RGBA(temp, m_frame.image);
		temp.release();
	}
}

void FrameWindow::FrameWindowImpl::AdjustWindowSize()
{
	ImVec2 winPos = ImGui::GetWindowPos();
	ImVec2 winSize = ImGui::GetWindowSize();
	ImVec2 viewportSize = ImGui::GetIO().DisplaySize;

	float adjust_w = winSize.x > viewportSize.x ? viewportSize.x : winSize.x;
	float adjust_h = winSize.y > viewportSize.y ? viewportSize.y : winSize.y;

	float adjust_x = 0.f;
	float adjust_y = 0.f;
	if (winPos.x < 0.f)
	{
		adjust_x = 0.f;
	}
	else if (winPos.x + winSize.x > viewportSize.x)
	{
		adjust_x = viewportSize.x - adjust_w;
	}
	else
	{
		adjust_x = winPos.x;
	}

	if (winPos.y < 0.f)
	{
		adjust_y = 0.f;
	}
	else if (winPos.y + winSize.y > viewportSize.y)
	{
		adjust_y = viewportSize.y - adjust_h;
	}
	else
	{
		adjust_y = winPos.y;
	}
	
	ImGui::SetWindowPos(ImVec2(adjust_x, adjust_y));
	ImGui::SetWindowSize(ImVec2(adjust_w, adjust_h));
}

void FrameWindow::FrameWindowImpl::Image2RGBA(const cv::Mat& input, cv::Mat& output)
{
	if (input.empty()) { output.release(); }
	int inputType = input.type();
	switch (inputType)
	{
	case CV_8UC1:
		cv::cvtColor(input, output, cv::ColorConversionCodes::COLOR_GRAY2RGBA);
		break;
	case CV_8UC3:
		cv::cvtColor(input, output, cv::ColorConversionCodes::COLOR_BGR2RGBA);
		break;
	case CV_8UC4:
		input.copyTo(output);
		break;
	default:
		throw std::runtime_error("Unsupported type!");
		break;
	}
}

void FrameWindow::FrameWindowImpl::UpdateTexture()
{
	std::unique_lock<std::mutex> lock(m_lock);
	if (m_lastFrameCaptureTime != m_frame.captureTime)
	{
		int width = m_frame.image.cols;
		int height = m_frame.image.rows;
		if (!m_frame.image.empty())
		{
			glBindTexture(GL_TEXTURE_2D, (intptr_t)m_frameBuffer);
			glTexImage2D(GL_TEXTURE_2D,
				0,
				GL_RGBA,
				width,
				height,
				0,
				GL_RGBA,
				GL_UNSIGNED_BYTE,
				m_frame.image.ptr());
		}
		
		if (m_latestResizedImageWidth != width || m_latestResizedImageHeight != height)
		{
			m_parent.ImageSizeChange(m_latestImageWidth, m_latestImageHeight, width, height);
		}

		m_latestImageWidth = width / m_resizeRatio; //reset to original
		m_latestImageHeight = height / m_resizeRatio; //reset to original
		if (m_latestImageWidth <= 0) m_latestImageWidth = 1;
		if (m_latestImageHeight <= 0) m_latestImageHeight = 1;
		m_latestAspectRatio = float(m_latestImageWidth) / float(m_latestImageHeight);
		m_lastFrameCaptureTime = m_frame.captureTime;
		m_latestResizedImageWidth = width;
		m_latestResizedImageHeight = height;
		if (m_latestResizedImageWidth <= 0) m_latestResizedImageWidth = 1;
		if (m_latestResizedImageHeight <= 0) m_latestResizedImageHeight = 1;
	}
	else
	{
	}
}

void FrameWindow::FrameWindowImpl::HandlePan(float dx, float dy)
{
	m_panX += dx;
	m_panY += dy;
}

void FrameWindow::FrameWindowImpl::SetPan(float panX, float panY)
{
	m_panX = panX;
	m_panY = panY;
}

void FrameWindow::FrameWindowImpl::HandleZoom(float factor)
{
	if (m_zoom + factor > 0.01f) //prevent 'overzooming'
	{
		m_zoom += factor;
	}
}

void FrameWindow::FrameWindowImpl::HandlePopUp()
{
	ImGui::OpenPopup("Control Graphics");
	GraphicalObject* pGraphicsObject = nullptr;
	if (ImGui::BeginPopup("Control Graphics"))
	{
		ImGui::Text("Control Elements:");
		ImGui::Separator();
		for (size_t i = 0; i < (size_t)Control::Graphics::NumberOfGraphics; i++)
		{
			Control::Graphics graphicsObject = (Control::Graphics)i;
			std::string graphicsName = Control::GetName(graphicsObject);
			if (ImGui::Selectable(graphicsName.c_str()))
			{
				pGraphicsObject = Control::CreateGraphicalObject(graphicsObject);
				if (pGraphicsObject != nullptr)
				{
					pGraphicsObject->Init(this->GetNextGraphicalObjectID());
					m_graphicalObjects.push_back(pGraphicsObject);
				}
				else
				{
					LogMessenger::Error("Something is wrong, cause returned graphical object is nullptr!");
				}
			}
		}		
		ImGui::EndPopup();
	}
}

float FrameWindow::FrameWindowImpl::RenderInputParameterLine(float currentValue, std::string name, bool round)
{
	std::string value_text = std::to_string(currentValue);
	static char value_text_char[32];
	std::strcpy(value_text_char, value_text.c_str());
	ImGui::InputText(name.c_str(), value_text_char, IM_ARRAYSIZE(value_text_char), ImGuiInputTextFlags_CharsDecimal);
	double inputValue = std::atof(value_text_char);
	if (round)
	{
		inputValue = std::round(inputValue);
	}
	return (float)inputValue;
}

void FrameWindow::FrameWindowImpl::SetCanvasPosition()
{
	int leftOffset = 5;
#ifdef RESETCOLORBUTTONS
	leftOffset += m_canvasXOffset;
#endif
#ifdef RESETCOLORBUTTONS
	int topOffset = 55; // for two line 55, for three line at least 85
#else
	int topOffset = 25; // for two line 55, for three line at least 85
#endif
	int rightOffset = 5;
	int bottomOffset = 5;

	m_canvasTopLeft.x = m_windowPos.x + leftOffset;
	m_canvasTopLeft.y =  m_windowPos.y + topOffset;
	m_canvasBottomRight.x = m_windowPos.x + m_windowSize.x - rightOffset;
	m_canvasBottomRight.y = m_canvasTopRight.y + m_windowSize.y - topOffset - bottomOffset;
	m_canvasTopRight.x = m_canvasBottomRight.x;
	m_canvasTopRight.y = m_canvasTopLeft.y;
	m_canvasBottomLeft.x = m_canvasTopLeft.x;
	m_canvasBottomLeft.y = m_canvasBottomRight.y;

	m_canvasWidth = m_canvasTopRight.x - m_canvasTopLeft.x;
	m_canvasHeight = m_canvasBottomLeft.y - m_canvasTopLeft.y;
}

void FrameWindow::FrameWindowImpl::DrawContextWindow()
{
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
	//ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
	ImGui::BeginChild("Coordinates", ImVec2(m_canvasXOffset * 0.9f, ImGui::GetContentRegionAvail().y), true, window_flags);
	if (ImGui::CollapsingHeader("Canvas", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::TreeNodeEx("Image", ImGuiTreeNodeFlags_DefaultOpen))
		{
			//remove spacing
			ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
				std::string imageWidth = "W: " + std::to_string((int)m_imageWidth) + " px";
				ImGui::TextColored(m_infoTextColor, imageWidth.c_str());
				std::string imageHeight = "H: " + std::to_string((int)m_imageHeight) + " px";
				ImGui::TextColored(m_infoTextColor, imageHeight.c_str());
			//add spacing
			ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
			ImGui::TreePop();
		}
		if (ImGui::TreeNodeEx("Mouse", ImGuiTreeNodeFlags_DefaultOpen))
		{
			//remove spacing
			ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Canvas");
			std::string canvasX = "X: " + std::to_string(m_mouseXInCanvas);
			ImGui::TextColored(m_infoTextColor, canvasX.c_str());
			std::string canvasY = "Y: " + std::to_string(m_mouseYInCanvas);
			ImGui::TextColored(m_infoTextColor, canvasY.c_str());
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Texture");
			std::string textureX = "X: " + std::to_string(m_mouseXInTexture);
			ImGui::TextColored(m_infoTextColor, textureX.c_str());
			std::string textureY = "Y: " + std::to_string(m_mouseYInTexture);
			ImGui::TextColored(m_infoTextColor, textureY.c_str());
			//add spacing
			ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
			ImGui::TreePop();
		}
		if (ImGui::TreeNodeEx("Control", ImGuiTreeNodeFlags_DefaultOpen))
		{
			//remove spacing
			ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());

			bool zoom_p = ImGui::Button("zoom+"); ImGui::SameLine();
			bool zoom_m = ImGui::Button("zoom-");
			bool pan_left = ImGui::ArrowButton("##left", ImGuiDir_Left); ImGui::SameLine();
			bool pan_right = ImGui::ArrowButton("##right", ImGuiDir_Right); ImGui::SameLine();
			bool pan_down = ImGui::ArrowButton("##down", ImGuiDir_Down); ImGui::SameLine();
			bool pan_up = ImGui::ArrowButton("##up", ImGuiDir_Up);
			bool reset_coord = ImGui::Button("Reset");
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Texture Pos");
			std::string panXText = "X: " + std::to_string(m_panX);
			ImGui::TextColored(m_infoTextColor, panXText.c_str());
			std::string panYText = "Y: " + std::to_string(m_panY);
			ImGui::TextColored(m_infoTextColor, panYText.c_str());
			std::string zoomText = "Z: " + std::to_string(m_zoom);
			ImGui::TextColored(m_infoTextColor, zoomText.c_str());
			if (zoom_p) HandleZoom(-m_zoom_factor);
			if (zoom_m) HandleZoom(m_zoom_factor);
			if (pan_up) HandlePan(0.f, m_pan_factor);
			if (pan_down) HandlePan(0.f, -m_pan_factor);
			if (pan_left) HandlePan(-m_pan_factor, 0.f);
			if (pan_right) HandlePan(m_pan_factor, 0.f);
			if (reset_coord) { m_zoom = 1.f; m_panX = m_panY = 0.f; }
			//add spacing
			ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
			ImGui::TreePop();
		}
		
	}
	if (ImGui::CollapsingHeader("Graphics", ImGuiTreeNodeFlags_DefaultOpen))
	{
		//Graphical elements information render
		RenderGraphicalObjectsParameters();
	}
	ImGui::EndChild();
	//ImGui::PopStyleVar();
}

void FrameWindow::FrameWindowImpl::RenderGraphicalObjectsParameters()
{
	std::vector<size_t> deletionIndeces;
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		//get type before
		Control::Graphics controGraphicsType = Control::Graphics::Rectangle;
		std::string figureName;
		if (dynamic_cast<RectangularGraphics*>(m_graphicalObjects[i]) != nullptr)
		{
			controGraphicsType = Control::Graphics::Rectangle;
			figureName = "Rectangle";
		}
		else if (dynamic_cast<PolygonGraphics*>(m_graphicalObjects[i]) != nullptr)
		{
			controGraphicsType = Control::Graphics::Polygon;
			figureName = "Polygon";
		}
		else
		{
			LogMessenger::Error("Undefined graphics!");
			continue;
		}

		std::string nodeName = figureName + " " + std::to_string(m_graphicalObjects[i]->GetID());
		if (ImGui::TreeNodeEx(nodeName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			//remove spacing
			ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
			char nameBuffer[32];
			strncpy(nameBuffer, m_graphicalObjects[i]->title.c_str(), sizeof(nameBuffer));
			ImGui::InputText("Name: ", nameBuffer, 32, ImGuiInputTextFlags_EnterReturnsTrue);
			m_graphicalObjects[i]->title = nameBuffer;

			switch (controGraphicsType)
			{
			case Control::Graphics::Rectangle:
			{
				RectangularGraphics* rect = dynamic_cast<RectangularGraphics*>(m_graphicalObjects[i]);
				std::string xText = "X: " + std::to_string(rect->x);
				ImGui::Text(xText.c_str());
				std::string yText = "Y: " + std::to_string(rect->y);
				ImGui::Text(yText.c_str());
				std::string wText = "Width: " + std::to_string(rect->width);
				ImGui::Text(wText.c_str());
				std::string hText = "Height: " + std::to_string(rect->height);
				ImGui::Text(hText.c_str());
				break;
			}
			case Control::Graphics::Polygon:
			{
				PolygonGraphics* poly = dynamic_cast<PolygonGraphics*>(m_graphicalObjects[i]);
				std::string polyPointCountText = "Points: " + std::to_string(poly->GetPointCount());
				ImGui::Text(polyPointCountText.c_str());
				break;
			}
			case Control::Graphics::NumberOfGraphics:
				//you shouldn't be here!
				break;
			default:
				//you shouldn't be here!
				break;
			}

			bool deletePressed = ImGui::Button("Delete");
			if (deletePressed)
			{
				deletionIndeces.push_back(i);
			}

			//add spacing
			ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
			ImGui::TreePop();
		}
	}
	//delete controls
	for (size_t i = 0; i < deletionIndeces.size(); i++)
	{
		m_graphicalObjects[i]->DeInit();
		delete m_graphicalObjects[i];
		m_graphicalObjects[i] = nullptr;
	}
	//remove element that are 'nullptr'
	if (deletionIndeces.size() > 0)
	{
		for (size_t i = 0; i < m_graphicalObjects.size(); i++)
		{
			if (m_graphicalObjects[i] == nullptr)
			{
				m_graphicalObjects.erase(m_graphicalObjects.begin() + i);
			}
		}
	}
}

void FrameWindow::FrameWindowImpl::CalculateQuad()
{
	//center of image
	float zoomingPtX = 0.5f;
	float zoomingPtY = 0.5f;

	m_uv_0.x = zoomingPtX + (0.0f - m_textureWidthOffset - zoomingPtX) * m_zoom + m_panX;
	m_uv_0.y = zoomingPtY + (0.0f - m_textureHeightOffset - zoomingPtY) * m_zoom + m_panY; //top left
	m_uv_1.x = zoomingPtX + (1.0f + m_textureWidthOffset - zoomingPtX) * m_zoom + m_panX;
	m_uv_1.y = zoomingPtY + (0.0f - m_textureHeightOffset - zoomingPtY) * m_zoom + m_panY; //top right
	m_uv_2.x = zoomingPtX + (1.0f + m_textureWidthOffset - zoomingPtX) * m_zoom + m_panX;
	m_uv_2.y = zoomingPtY + (1.0f + m_textureHeightOffset - zoomingPtY) * m_zoom + m_panY; //bottom right
	m_uv_3.x = zoomingPtX + (0.0f - m_textureWidthOffset - zoomingPtX) * m_zoom + m_panX;
	m_uv_3.y = zoomingPtY + (1.0f + m_textureHeightOffset - zoomingPtY) * m_zoom + m_panY; //bottom left
}

void FrameWindow::FrameWindowImpl::HandleMouseManipulations()
{
	SortGraphicalObjectByUpdateTime();

	ImGuiIO& io = ImGui::GetIO();
	float mouseX = io.MousePos.x;
	float mouseY = io.MousePos.y;

	bool cursorOnCanvas = (m_canvasTopLeft.x <= mouseX) && (m_canvasTopRight.x > mouseX) && (m_canvasTopLeft.y <= mouseY) && (m_canvasBottomLeft.y > mouseY);
	
	bool windowHovered = ImGui::IsWindowHovered();
	bool windowFocused = ImGui::IsWindowFocused();
	//update status of focused canvas
	m_canvasFocused = cursorOnCanvas && windowHovered;

	/* DEPRECATED CAUSE MOVEMENT STATUS WILL BE UPDATED FROM GLOBAL FIELD WITH DECISION FROM EVERY FRAME WINDOW
	//enable/disable window drag with left mouse button down
	io.ConfigWindowsMoveFromTitleBarOnly = m_canvasFocused;
	*/

	bool mouseLeftReleased = ImGui::IsMouseReleased(0);
	bool mouseRightReleased = ImGui::IsMouseReleased(1);
	if (mouseRightReleased) { m_wasMouseClickedInsideCanvas = false; }
	//First get object that mouse in on and then set an action of release
	if (mouseLeftReleased)
	{
		for (size_t i = 0; i < m_graphicalObjects.size(); i++)
		{
			m_graphicalObjects[i]->MouseLeftState(false);
		}
	}
	if (mouseRightReleased)
	{
		for (size_t i = 0; i < m_graphicalObjects.size(); i++)
		{
			m_graphicalObjects[i]->MouseRightState(false);
		}
	}

	if (m_canvasFocused) // check also if active
	{
		if (io.MouseWheel > 0.f)
		{ 
			HandleZoom(-m_zoom_factor);
		} 
		else if (io.MouseWheel < 0.f) 
		{ 
			HandleZoom(m_zoom_factor); 
		}
		if (io.MouseDown)
		{
			//save last pan and mouse coordinates if right mouse button is clicked
			if (ImGui::IsMouseClicked(1))
			{
				m_mouseDownX = mouseX;
				m_mouseDownY = mouseY;
				m_panOnMouseDownX = m_panX;
				m_panOnMouseDownY = m_panY;
				m_wasMouseClickedInsideCanvas = true;
			}
			if (ImGui::IsMouseDown(1) && m_wasMouseClickedInsideCanvas)
			{
				float width_res = m_canvasTopRight.x - m_canvasBottomLeft.x;
				if (width_res <= 0.f) width_res = 1.f;
				float height_res = m_canvasBottomLeft.y - m_canvasTopLeft.y;
				if (height_res <= 0.f) height_res = 1.f;
				float deltaX_ = m_panOnMouseDownX + (m_mouseDownX - mouseX) / width_res * (1.f + m_textureWidthOffset * 2.f) * m_zoom;
				float deltaY_ = m_panOnMouseDownY + (m_mouseDownY - mouseY) / height_res * (1.f + m_textureHeightOffset * 2.f) * m_zoom;
				SetPan(deltaX_, deltaY_);
			}
			if (ImGui::IsMouseClicked(0) && ImGui::GetIO().KeyCtrl)
			{
				m_mouseDownCtrlX = mouseX;
				m_mouseDownCtrlY = mouseY;
			}
			if (ImGui::GetIO().KeyCtrl) //make focused object to indicate more significantly
			{
				for (size_t i = 0; i < m_graphicalObjects.size(); i++)
				{
					m_graphicalObjects[i]->SetFocus(true);
				}
			}
			if (ImGui::IsMouseDown(0) && ImGui::GetIO().KeyCtrl)
			{
				//offset all region
				float deltaX_ = -(m_mouseDownCtrlX - mouseX) / m_latestPixelRatio;
				float deltaY_ = -(m_mouseDownCtrlY - mouseY) / m_latestPixelRatio;
				for (size_t i = 0; i < m_graphicalObjects.size(); i++)
				{
					m_graphicalObjects[i]->Offset(deltaX_, deltaY_);
				}
				//save latest position and offset next time from this point
				m_mouseDownCtrlX = mouseX;
				m_mouseDownCtrlY = mouseY;
				return;
			}
		}
		//Interested only what happened inside canvas region
		//Register again all the clicks that happened to the graphical objects inside window

		//First check released buttons and do action according

		//Control objects is mouse is down
		bool mouseLeftDown = ImGui::IsMouseDown(0);
		bool mouseRightDown = ImGui::IsMouseDown(1);
		//First check which object is controlled and if nothing is, check which is focused
		GraphicalObject* controlledObject = nullptr;
		for (size_t i = 0; i < m_graphicalObjects.size(); i++)
		{
			if (m_graphicalObjects[i]->IsControlled())
			{
				controlledObject = m_graphicalObjects[i];
				break;//no need to search for more, because object was found
			}
		}
		if (controlledObject != nullptr && mouseLeftDown)
		{
			if (controlledObject->IsActive()) { controlledObject->OnLeftDown(m_mouseXInTexture, m_mouseYInTexture, m_canvas2TextureConverter, m_canvasTopLeft[0], m_canvasTopLeft[1], m_latestPixelRatio, m_resizeRatio); }
		}
		else if (controlledObject != nullptr && mouseRightDown)
		{
			if (controlledObject->IsActive()) { controlledObject->OnRightDown(m_mouseXInTexture, m_mouseYInTexture, m_canvas2TextureConverter, m_canvasTopLeft[0], m_canvasTopLeft[1], m_latestPixelRatio, m_resizeRatio); }
		}
		//if something is found, no need to continue doing further
		if (controlledObject != nullptr)
		{
			return;
		}
		
		//Register mouse click
		//Get control element that is in on the mouse
		GraphicalObject* inRangeObject = nullptr;
		for (size_t i = 0; i < m_graphicalObjects.size(); i++)
		{
			if (m_graphicalObjects[i]->IsMouseOnObject(m_mouseXInTexture, m_mouseYInTexture, m_canvas2TextureConverter, m_canvasTopLeft[0], m_canvasTopLeft[1], m_latestPixelRatio, m_resizeRatio))
			{
				inRangeObject = m_graphicalObjects[i];
				break;//no need to search for more, because object was found
			}
		}
		//If object is found, register click or focused
		//Priority will go as following mouse_click_left->mouse_click_right->hovering_over
		bool mouseLeftClicked = ImGui::IsMouseClicked(0);
		bool mouseRightClicked = ImGui::IsMouseClicked(1);
		if (inRangeObject != nullptr)
		{
			if (mouseLeftClicked)
			{
				inRangeObject->MouseLeftState(true);
				inRangeObject->MouseLeftClickUpdate(m_mouseXInTexture, m_mouseYInTexture, m_resizeRatio);
				//turn of focus of all other objects
				for (auto& graphicalObject : m_graphicalObjects)
				{
					if (inRangeObject->GetID() != graphicalObject->GetID())
					{
						graphicalObject->SetFocus(false);
					}
				}
			}
			else if (mouseRightClicked)
			{
				inRangeObject->MouseRightState(true);
				inRangeObject->MouseRightClickUpdate(m_mouseXInTexture, m_mouseYInTexture, m_resizeRatio);
				//turn of focus of all other objects
				for (auto& graphicalObject : m_graphicalObjects)
				{
					if (inRangeObject->GetID() != graphicalObject->GetID())
					{
						graphicalObject->MouseRightState(false);
					}
				}
			}
			else //hovered over
			{
				inRangeObject->MouseHoveredOver(true);
				inRangeObject->MouseCursorHoverUpdate(m_mouseXInTexture, m_mouseYInTexture, m_resizeRatio);
				for (auto& graphicalObject : m_graphicalObjects)
				{
					if (inRangeObject->GetID() != graphicalObject->GetID())
					{
						graphicalObject->MouseHoveredOver(false);
					}
				}
			}
		}
		else
		{
			for (auto& graphicalObject : m_graphicalObjects)
			{
				graphicalObject->MouseHoveredOver(false);
			}
			if (mouseLeftClicked)
			{
				for (auto& graphicalObject : m_graphicalObjects)
				{
					graphicalObject->SetFocus(false);
				}
			}
			if (mouseRightClicked)
			{
				for (auto& graphicalObject : m_graphicalObjects)
				{
					graphicalObject->MouseRightState(false);
				}
			}
		}
	}
}

void FrameWindow::FrameWindowImpl::DrawGraphicalElements()
{
	ImGui::GetWindowDrawList()->PushClipRect(m_canvasTopLeft, m_canvasBottomRight, true);
	
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		m_graphicalObjects[i]->Render(m_canvas2TextureConverter, m_canvasTopLeft[0], m_canvasTopLeft[1], m_latestPixelRatio, m_resizeRatio_);
	}
	ImGui::GetWindowDrawList()->PopClipRect();
}

void FrameWindow::FrameWindowImpl::CalculateMouseCoordinateOnTexture()
{
	ImGuiIO& io = ImGui::GetIO();
	m_mouseXInCanvas = io.MousePos.x - m_canvasTopLeft.x;
	m_mouseYInCanvas = io.MousePos.y - m_canvasTopLeft.y;
	Canvas2TextureCoord(m_mouseXInCanvas, m_mouseYInCanvas, m_mouseXInTexture, m_mouseYInTexture);
}

void FrameWindow::FrameWindowImpl::UpdateConverter()
{
	m_imageWidth = m_latestImageWidth == 0 ? 1920.f : m_latestImageWidth;
	m_imageHeight = m_latestImageHeight == 0 ? 1080.f : m_latestImageHeight;

	m_widthRatio = m_imageWidth / m_canvasWidth * (1.f + m_textureWidthOffset * 2.f) * m_zoom;
	m_heightRatio = m_imageHeight / m_canvasHeight * (1.f + m_textureHeightOffset * 2.f) * m_zoom;
	//std::cout << m_widthRatio << " " << m_heightRatio << std::endl;
}

void FrameWindow::FrameWindowImpl::ChangeCanvasColor(ImVec4 color)
{
	float borderColor[] = { color.x, color.y, color.z, 1.0f };
	glBindTexture(GL_TEXTURE_2D, (intptr_t)m_frameBuffer);
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
}

void FrameWindow::FrameWindowImpl::Canvas2TextureCoord(float canvasX, float canvasY, float& textureX, float& textureY)
{
	textureX = canvasX * m_widthRatio + m_imageWidth * m_uv_0.x;
	textureY = canvasY * m_heightRatio + m_imageHeight * m_uv_0.y;
}

void FrameWindow::FrameWindowImpl::Texture2CanvasCoord(float textureX, float textureY, float& canvasX, float& canvasY)
{
	canvasX = (textureX - m_imageWidth * m_uv_0.x) / m_widthRatio;
	canvasY = (textureY - m_imageHeight * m_uv_0.y) / m_heightRatio;
}

void FrameWindow::FrameWindowImpl::DrawFrameOnFocus()
{
	int timestamp = (int)Timer::GetTimeStamp() % 1000; //miliseconds only matter
	float thickness = 1.f;
	ImU32 color = IM_COL32(0, 0, 0, 255);
	if (m_canvasFocused)
	{
		uchar firstGlowValue = (uchar)(std::sin((float)timestamp / MAGICGLOWNUMBER) * 100.f) + 100.f; //make value between 150 and 250
		uchar secondGlowValue = (uchar)(std::cos((float)timestamp / MAGICGLOWNUMBER) * 100.f) + 150.f; //make value between 150 and 250
		//render differently if control key on keyboard is pressed
		if (ImGui::GetIO().KeyCtrl)
		{
			color = IM_COL32(secondGlowValue, 150, firstGlowValue, 255);
			thickness = 5.f;
		}
		else
		{
			color = IM_COL32(secondGlowValue, 100, firstGlowValue, 150);
			thickness = 2.f;
		}
	}
	ImGui::GetWindowDrawList()->AddRect(m_canvasTopLeft,
		m_canvasBottomRight,
		color,
		0.f,
		0,
		thickness);
}

void FrameWindow::FrameWindowImpl::SortGraphicalObjectByUpdateTime()
{
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		for (size_t j = i + 1; j < m_graphicalObjects.size(); j++)
		{
			if (m_graphicalObjects[i]->GetLastUpdateTime() < m_graphicalObjects[j]->GetLastUpdateTime())
			{
				GraphicalObject* temp = m_graphicalObjects[j];
				m_graphicalObjects[j] = m_graphicalObjects[i];
				m_graphicalObjects[i] = temp;
			}
		}
	}
}

unsigned int FrameWindow::FrameWindowImpl::GetNextGraphicalObjectID()
{
	int highestID = 0;
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		int currentObjectID = m_graphicalObjects[i]->GetID();
		if (currentObjectID > highestID)
		{
			highestID = currentObjectID;
		}
	}
	//increment id to be next
	highestID++;
	return (unsigned int)highestID;
}

void FrameWindow::FrameWindowImpl::AdjustGraphicalObjectsIDs(int deletedID)
{
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		int currentObjectID = m_graphicalObjects[i]->GetID();
		if (currentObjectID > deletedID)
		{
			m_graphicalObjects[i]->SetID(currentObjectID++);
		}
	}
}

void FrameWindow::FrameWindowImpl::UploadInitialImage()
{
	cv::Mat image = cv::Mat(cv::Size(640, 480), CV_8UC3);
	image.setTo(0);
	if (!image.empty())
	{
		cv::cvtColor(image, m_frame.image, cv::ColorConversionCodes::COLOR_BGR2RGBA);
		m_latestImageWidth = image.cols;
		m_latestImageHeight = image.rows;
		image.release();
	}
	else
	{
		LogMessenger::Error("Initial image in frame window is not loaded!");
	}
}

void FrameWindow::FrameWindowImpl::ResetCanvasParameters()
{
	m_canvasTopLeft.x = 0.f;
	m_canvasTopLeft.y = 0.f;
	m_canvasTopRight.x = 0.f;
	m_canvasTopRight.y = 0.f;
	m_canvasBottomRight.x = 0.f;
	m_canvasBottomRight.y = 0.f;
	m_canvasBottomLeft.x = 0.f;
	m_canvasBottomLeft.y = 0.f;
	m_uv_0.x = 0.f;
	m_uv_0.y = 0.f;
	m_uv_1.x = 1.f;
	m_uv_1.y = 0.f;
	m_uv_2.x = 1.f;
	m_uv_2.y = 1.f;
	m_uv_3.x = 0.f;
	m_uv_3.y = 1.f;
	m_textureWidthOffset = 0.f;
	m_textureHeightOffset = 0.f;
	m_widthRatio = 1.f;
	m_heightRatio = 1.f;
	m_canvasWidth = m_canvasTopRight.x - m_canvasTopLeft.x;
	m_canvasHeight = m_canvasBottomLeft.y - m_canvasTopLeft.y;
	m_imageWidth = 1920.f;
	m_imageHeight = 1080.f;
}

void FrameWindow::FrameWindowImpl::CopyCurrentGeometriesToOld()
{
	//clear vector before copying
	for (size_t i = 0; i < m_oldGraphicalElements.size(); i++)
	{
		delete m_oldGraphicalElements[i];
	}
	m_oldGraphicalElements.clear();

	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		//check what derived class object is
		RectangularGraphics* pRect = dynamic_cast<RectangularGraphics*>(m_graphicalObjects[i]);
		if (pRect != nullptr)
		{
			GraphicalObject* pRectCopy = new RectangularGraphics(*pRect);
			m_oldGraphicalElements.push_back(pRectCopy);
			continue;
		}
		PolygonGraphics* pPoly = dynamic_cast<PolygonGraphics*>(m_graphicalObjects[i]);
		if (pPoly != nullptr)
		{
			GraphicalObject* pPolyCopy = new PolygonGraphics(*pPoly);
			m_oldGraphicalElements.push_back(pPolyCopy);
			continue;
		}
		CircleGraphics* pCirc = dynamic_cast<CircleGraphics*>(m_graphicalObjects[i]);
		if (pCirc != nullptr)
		{
			GraphicalObject* pCircCopy = new CircleGraphics(*pCirc);
			m_oldGraphicalElements.push_back(pCircCopy);
			continue;
		}
		//you shouldn't be here
		LogMessenger::Error("Unspecified type in object copy!");
	}
}

std::vector<int> FrameWindow::FrameWindowImpl::GetChangedGraphicalObjectIndeces()
{
	std::vector<int> changeGeometriesIndeces;
	//if the size of old and new vector are different, generate event with additional index -1
	if (m_oldGraphicalElements.size() != m_graphicalObjects.size())
	{
		changeGeometriesIndeces.push_back(-1);
	}
	for (size_t i = 0; i < m_graphicalObjects.size(); i++)
	{
		int currentObjectIndex = m_graphicalObjects[i]->GetID();
		bool objectFound = false;
		for (size_t j = 0; j < m_oldGraphicalElements.size(); j++)
		{
			int oldObjectIndex = m_oldGraphicalElements[j]->GetID();
			if (currentObjectIndex == oldObjectIndex)
			{
				objectFound = true;
				//try to compare
				if (*m_graphicalObjects[i] == *m_oldGraphicalElements[j])
				{
					
				}
				else
				{
					changeGeometriesIndeces.push_back(oldObjectIndex);
					break;
				}
			}
		}
		if (!objectFound) //add it, because it is probably new one
		{
			changeGeometriesIndeces.push_back(i);
		}
	}
	return changeGeometriesIndeces;
}

int FrameWindow::FrameWindowImpl::GetUniqueId()
{
	return ++m_geometriesIDCount;
}

FrameWindow::FrameWindowImpl::~FrameWindowImpl()
{
	DeInit();
}

float FrameWindow::FrameWindowImpl::GetRescaleRatio()
{
	return m_latestPixelRatio;
}
