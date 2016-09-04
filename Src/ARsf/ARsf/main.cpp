
#include "stdafx.h"
#include <opencv2/core/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/aruco.hpp>
#include <iostream>


#ifdef _DEBUG
	#pragma comment(lib, "opencv_core310d.lib")
	#pragma comment(lib, "opencv_imgcodecs310d.lib")
	#pragma comment(lib, "opencv_videoio310d.lib")
	#pragma comment(lib, "opencv_highgui310d.lib")
	#pragma comment(lib, "opencv_imgproc310d.lib")
	#pragma comment(lib, "opencv_calib3d310d.lib")
	#pragma comment(lib, "opencv_aruco310d.lib")
#else
	#pragma comment(lib, "opencv_core310.lib")
	#pragma comment(lib, "opencv_imgcodecs310.lib")
	#pragma comment(lib, "opencv_videoio310.lib")
	#pragma comment(lib, "opencv_highgui310.lib")
	#pragma comment(lib, "opencv_imgproc310.lib")
	#pragma comment(lib, "opencv_calib3d310.lib")
	#pragma comment(lib, "opencv_aruco310.lib")
#endif


using namespace graphic;
using namespace cv;


class cViewer : public framework::cGameMain
{
public:
	cViewer();
	virtual ~cViewer();

	virtual bool OnInit() override;
	virtual void OnUpdate(const float elapseT) override;
	virtual void OnRender(const float elapseT) override;
	virtual void OnShutdown() override;
	virtual void OnMessageProc(UINT message, WPARAM wParam, LPARAM lParam) override;


private:
	LPD3DXSPRITE m_sprite;
	graphic::cSprite *m_videoSprite;
	graphic::cCharacter m_character;

	VideoCapture m_inputVideo;
	Mat m_camImage;

	Ptr<aruco::DetectorParameters> m_detectorParams;
	Ptr<aruco::Dictionary> m_dictionary;
	Mat m_camMatrix;
	Mat m_distCoeffs;
	float m_markerLength = 75;

	Matrix44 m_zealotCameraView;

	Vector3 m_pos;
	POINT m_curPos;
	bool m_LButtonDown;
	bool m_RButtonDown;
	bool m_MButtonDown;
	Matrix44 m_rotateTm;

	Vector3 m_boxPos;
};

INIT_FRAMEWORK(cViewer);

const int WINSIZE_X = 640;
const int WINSIZE_Y = 480;

cViewer::cViewer() :
	m_character(1000)
{
	m_windowName = L"ARUCO Zealot";
	const RECT r = { 0, 0, WINSIZE_X, WINSIZE_Y };
	m_windowRect = r;
	m_LButtonDown = false;
	m_RButtonDown = false;
	m_MButtonDown = false;
}

cViewer::~cViewer()
{
	SAFE_DELETE(m_videoSprite);
	m_sprite->Release();
	graphic::ReleaseRenderer();
}


static bool readCameraParameters(string filename, Mat &camMatrix, Mat &distCoeffs) {
	FileStorage fs(filename, FileStorage::READ);
	if (!fs.isOpened())
		return false;
	fs["camera_matrix"] >> camMatrix;
	fs["distortion_coefficients"] >> distCoeffs;
	return true;
}


static bool readDetectorParameters(string filename, Ptr<aruco::DetectorParameters> &params) {
	FileStorage fs(filename, FileStorage::READ);
	if (!fs.isOpened())
		return false;
	fs["adaptiveThreshWinSizeMin"] >> params->adaptiveThreshWinSizeMin;
	fs["adaptiveThreshWinSizeMax"] >> params->adaptiveThreshWinSizeMax;
	fs["adaptiveThreshWinSizeStep"] >> params->adaptiveThreshWinSizeStep;
	fs["adaptiveThreshConstant"] >> params->adaptiveThreshConstant;
	fs["minMarkerPerimeterRate"] >> params->minMarkerPerimeterRate;
	fs["maxMarkerPerimeterRate"] >> params->maxMarkerPerimeterRate;
	fs["polygonalApproxAccuracyRate"] >> params->polygonalApproxAccuracyRate;
	fs["minCornerDistanceRate"] >> params->minCornerDistanceRate;
	fs["minDistanceToBorder"] >> params->minDistanceToBorder;
	fs["minMarkerDistanceRate"] >> params->minMarkerDistanceRate;
	fs["doCornerRefinement"] >> params->doCornerRefinement;
	fs["cornerRefinementWinSize"] >> params->cornerRefinementWinSize;
	fs["cornerRefinementMaxIterations"] >> params->cornerRefinementMaxIterations;
	fs["cornerRefinementMinAccuracy"] >> params->cornerRefinementMinAccuracy;
	fs["markerBorderBits"] >> params->markerBorderBits;
	fs["perspectiveRemovePixelPerCell"] >> params->perspectiveRemovePixelPerCell;
	fs["perspectiveRemoveIgnoredMarginPerCell"] >> params->perspectiveRemoveIgnoredMarginPerCell;
	fs["maxErroneousBitsInBorderRate"] >> params->maxErroneousBitsInBorderRate;
	fs["minOtsuStdDev"] >> params->minOtsuStdDev;
	fs["errorCorrectionRate"] >> params->errorCorrectionRate;
	return true;
}


bool cViewer::OnInit()
{
	// start craft 2
	// zealot
	{
		m_character.Create(m_renderer, "zealot.dat");
		if (graphic::cMesh* mesh = m_character.GetMesh("Sphere001"))
			mesh->SetRender(false);
		m_character.SetShader(graphic::cResourceManager::Get()->LoadShader(m_renderer, 
			"hlsl_skinning_using_texcoord_sc2.fx"));
		m_character.SetRenderShadow(true);

		vector<sActionData> actions;
		actions.reserve(16);
		actions.push_back(sActionData(CHARACTER_ACTION::NORMAL, "zealot_stand.ani"));
		actions.push_back(sActionData(CHARACTER_ACTION::RUN, "zealot_walk.ani"));
		actions.push_back(sActionData(CHARACTER_ACTION::ATTACK, "zealot_attack.ani"));
		m_character.SetActionData(actions);
		m_character.Action( CHARACTER_ACTION::RUN );
	}

	D3DXCreateSprite(m_renderer.GetDevice(), &m_sprite);
	m_videoSprite = new graphic::cSprite(m_sprite, 0);
	m_videoSprite->SetTexture(m_renderer, "kim.jpg");
	m_videoSprite->SetPos(Vector3(0, 0, 0));


	GetMainCamera()->Init(&m_renderer);
	GetMainCamera()->SetCamera(Vector3(10, 10, -10), Vector3(0, 0, 0), Vector3(0, 1, 0));
	GetMainCamera()->SetProjection(D3DX_PI / 4.f, (float)WINSIZE_X / (float)WINSIZE_Y, 1.f, 10000.0f);

	GetMainLight().Init(cLight::LIGHT_DIRECTIONAL);
	GetMainLight().SetPosition(Vector3(5, 5, 5));
	GetMainLight().SetDirection(Vector3(1, -1, 1).Normal());

	m_renderer.GetDevice()->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
	m_renderer.GetDevice()->LightEnable(0, true);


	m_detectorParams = aruco::DetectorParameters::create();
	readDetectorParameters("detector_params.yml", m_detectorParams);
	m_detectorParams->doCornerRefinement = true; // do corner refinement in markers
	m_dictionary = aruco::getPredefinedDictionary(aruco::DICT_ARUCO_ORIGINAL);

	readCameraParameters("camera.yml", m_camMatrix, m_distCoeffs);

	m_inputVideo.open(0);

	return true;
}


void cViewer::OnUpdate(const float elapseT)
{
	if (!m_inputVideo.grab())
		return;

	Mat image;
	m_inputVideo.retrieve(image);
	if (!image.data)
		return;

	vector< int > ids;
	vector< vector< Point2f > > corners, rejected;
	vector< Vec3d > rvecs, tvecs;

	// detect markers and estimate pose
	aruco::detectMarkers(image, m_dictionary, corners, ids, m_detectorParams, rejected);

	if (ids.size() > 0) 
	{
		aruco::estimatePoseSingleMarkers(corners, m_markerLength, m_camMatrix, m_distCoeffs, rvecs, tvecs);
		aruco::drawDetectedMarkers(image, corners, ids);

		for (unsigned int i = 0; i < ids.size(); i++)
		{
			aruco::drawAxis(image, m_camMatrix, m_distCoeffs, rvecs[i], tvecs[i], m_markerLength * 0.5f);

			// change aruco space to direct x space
			Mat rot;
			Rodrigues(rvecs[i], rot);
			Mat invRot;
			transpose(rot, invRot); // inverse matrix
			double *pinvR = invRot.ptr<double>();

			Matrix44 tm;
			tm.m[0][0] = -(float)pinvR[0];
			tm.m[0][1] = (float)pinvR[1];
			tm.m[0][2] = -(float)pinvR[2];
			
			tm.m[1][0] = -(float)pinvR[3];
			tm.m[1][1] = (float)pinvR[4];
			tm.m[1][2] = -(float)pinvR[5];

			tm.m[2][0] = -(float)pinvR[6];
			tm.m[2][1] = (float)pinvR[7];
			tm.m[2][2] = -(float)pinvR[8];

			Matrix44 rot2;
			rot2.SetRotationX(ANGLE2RAD(-90)); // y-z axis change

			Matrix44 trans;
			trans.SetPosition( Vector3((float)tvecs[i][0], -(float)tvecs[i][1], (float)tvecs[i][2]) * 0.01f );

			m_zealotCameraView = rot2 * tm * trans;
		}
	}

	// display camera image to DirectX Texture
	D3DLOCKED_RECT lockRect;
	m_videoSprite->GetTexture()->Lock(lockRect);
	if (lockRect.pBits)
	{
		Mat BGRA = image.clone();
		cvtColor(image, BGRA, CV_BGR2BGRA, 4);
		const size_t sizeInBytes2 = BGRA.step[0] * BGRA.rows;
		memcpy(lockRect.pBits, BGRA.data, sizeInBytes2);
		m_videoSprite->GetTexture()->Unlock();
	}

	GetMainCamera()->Update();

	m_character.Update(elapseT);
}


void cViewer::OnRender(const float elapseT)
{
	//화면 청소
	if (SUCCEEDED(m_renderer.GetDevice()->Clear(0,NULL,D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,D3DCOLOR_XRGB(150, 150, 150),1.0f,0)))
	{
		m_renderer.GetDevice()->BeginScene();

		m_renderer.RenderGrid();
		m_renderer.RenderAxis();

		m_renderer.GetDevice()->SetRenderState(D3DRS_ZENABLE, 0);
		m_videoSprite->Render(m_renderer, Matrix44::Identity);
		m_renderer.GetDevice()->SetRenderState(D3DRS_ZENABLE, 1);

 		GetMainCamera()->SetViewMatrix(m_zealotCameraView);
 		m_renderer.GetDevice()->SetTransform(D3DTS_VIEW, (D3DMATRIX*)&m_zealotCameraView);
		m_character.Render(m_renderer, Matrix44::Identity);

		m_renderer.RenderFPS();

		//랜더링 끝
		m_renderer.GetDevice()->EndScene();
		m_renderer.GetDevice()->Present(NULL, NULL, NULL, NULL);
	}
}


void cViewer::OnShutdown()
{

}


void cViewer::OnMessageProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_MOUSEWHEEL:
	{
		int fwKeys = GET_KEYSTATE_WPARAM(wParam);
		int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
		dbg::Print("%d %d", fwKeys, zDelta);

		const float len = graphic::GetMainCamera()->GetDistance();
		float zoomLen = (len > 100) ? 50 : (len / 4.f);
		if (fwKeys & 0x4)
			zoomLen = zoomLen / 10.f;

		graphic::GetMainCamera()->Zoom((zDelta < 0) ? -zoomLen : zoomLen);
	}
	break;

	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_BACK:
			break;
		case VK_TAB:
		{
			static bool flag = false;
			m_renderer.GetDevice()->SetRenderState(D3DRS_CULLMODE, flag ? D3DCULL_CCW : D3DCULL_NONE);
			m_renderer.GetDevice()->SetRenderState(D3DRS_FILLMODE, flag ? D3DFILL_SOLID : D3DFILL_WIREFRAME);
			flag = !flag;
		}
		break;

		case VK_LEFT: m_boxPos.x -= 10.f; break;
		case VK_RIGHT: m_boxPos.x += 10.f; break;
		case VK_UP: m_boxPos.z += 10.f; break;
		case VK_DOWN: m_boxPos.z -= 10.f; break;
		}
		break;

	case WM_LBUTTONDOWN:
	{
		m_LButtonDown = true;
		m_curPos.x = LOWORD(lParam);
		m_curPos.y = HIWORD(lParam);
	}
	break;

	case WM_LBUTTONUP:
		m_LButtonDown = false;
		break;

	case WM_RBUTTONDOWN:
	{
		m_RButtonDown = true;
		m_curPos.x = LOWORD(lParam);
		m_curPos.y = HIWORD(lParam);
	}
	break;

	case WM_RBUTTONUP:
		m_RButtonDown = false;
		break;

	case WM_MBUTTONDOWN:
		m_MButtonDown = true;
		m_curPos.x = LOWORD(lParam);
		m_curPos.y = HIWORD(lParam);
		break;

	case WM_MBUTTONUP:
		m_MButtonDown = false;
		break;

	case WM_MOUSEMOVE:
	{
		if (m_RButtonDown)
		{
			POINT pos = { LOWORD(lParam), HIWORD(lParam) };
			const int x = pos.x - m_curPos.x;
			const int y = pos.y - m_curPos.y;
			m_curPos = pos;

			graphic::GetMainCamera()->Yaw2(x * 0.005f);
			graphic::GetMainCamera()->Pitch2(y * 0.005f);
		}
		else if (m_MButtonDown)
		{
			const POINT point = { LOWORD(lParam), HIWORD(lParam) };
			const POINT pos = { point.x - m_curPos.x, point.y - m_curPos.y };
			m_curPos = point;

			const float len = graphic::GetMainCamera()->GetDistance();
			graphic::GetMainCamera()->MoveRight(-pos.x * len * 0.001f);
			graphic::GetMainCamera()->MoveUp(pos.y * len * 0.001f);
		}

	}
	break;
	}
}

