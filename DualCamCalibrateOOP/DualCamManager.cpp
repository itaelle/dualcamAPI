#include "stdafx.h"
#include "DualCamManager.h"
#include <fstream>
#include <iostream>
#include <Windows.h>
#include <dshow.h>
#include <thread>
#include <GL\GL.H>
#include <GL\GLU.H>
#include <SFML\Window.hpp>
#include <SFML\Graphics.hpp>

DualCamManager::DualCamManager(std::string _fileName, CvSize sizeCam1, CvSize sizeCam2, int nx, int ny, int useUncalibrated, double _squareSize) : fileName(_fileName), SizeCam1(sizeCam1), SizeCam2(sizeCam2)
{
	FILE * f = fopen(this->fileName.c_str(), "rt");
	iscalibrated = true;
	calibManager = new CalibrationManager(fileName, nx, ny, useUncalibrated, _squareSize);

	if (!f)
		iscalibrated = false;
	else
	{
		if (checkFileQuality(f))
			fclose(f);
		else
			iscalibrated = false;
	}
	cam1Resize = cvCreateImage(SizeCam1, 8, 3);
	cam2Resize = cvCreateImage(SizeCam2, 8, 3);
	cam1Gray = cvCreateImage(SizeCam1, 8, 1);
	cam2Gray = cvCreateImage(SizeCam2, 8, 1);
	stopThread = false;
	threadRunning = false;
	
}


DualCamManager::~DualCamManager(void)
{
	stopThread = true;
	cvReleaseImage(&cam1Resize);
	cvReleaseImage(&cam2Resize);
	cvReleaseImage(&cam1Gray);
	cvReleaseImage(&cam2Gray);
	cvReleaseCapture(&cap1);
	cvReleaseCapture(&cap2);
	cvReleaseMat(&depthData);
	//cvReleaseImage(&depthData);
	//dualCamThread->join();
	if (threadRunning)
		dualCamThread->detach();
	delete dualCamThread;
}

bool DualCamManager::isCalibrated()const
{
	return iscalibrated;
}


void DualCamManager::saveCalibrationImages(int nx, int ny)
{
	chessX = nx;
	chessY = ny;
	CvPoint2D32f * corners1 = (CvPoint2D32f*)malloc(sizeof(CvPoint2D32f) * (chessX * chessY) + 1);
	CvPoint2D32f * corners2 = (CvPoint2D32f*)malloc(sizeof(CvPoint2D32f) * (chessX * chessY) + 1);
	int key = -1;
	std::ofstream file;
	int count = 0;
	file.open(this->fileName);
	cvNamedWindow("Cam1");
	cvNamedWindow("Cam2");
	std::cout << "in save calib images"<<std::endl;
	while (key != 27)
	{
		cam1 = cvQueryFrame(cap1);
		cam2 = cvQueryFrame(cap2);

		if (cam1 && cam2)
		{
			cvResize(cam1, cam1Resize);
			cvResize(cam2, cam2Resize);
			cvCvtColor(cam1Resize, cam1Gray, CV_RGB2GRAY);
			cvCvtColor(cam2Resize, cam2Gray, CV_RGB2GRAY);

			int found1 = cvFindChessboardCorners(cam1Gray, cvSize(chessX, chessY), corners1);
			int found2 = cvFindChessboardCorners(cam2Gray, cvSize(chessX, chessY), corners2);
			cvDrawChessboardCorners(cam1Resize, cvSize(chessX, chessY), corners1, chessX * chessY, 1);
			cvDrawChessboardCorners(cam2Resize, cvSize(chessX, chessY), corners2, chessX * chessY, 1);
			if (found1 == 1 && found2 == 1 && count < 15)
			{
				if (count < 15)
				{
					std::string imageNameLeft = "ImageLeft";
					std::string imageNameRight = "ImageRight";
					std::ostringstream ss1;
					ss1 << count;
					imageNameLeft += ss1.str();
					imageNameRight += ss1.str();
					imageNameLeft += ".jpg";
					imageNameRight += ".jpg";
					cvSaveImage(imageNameLeft.c_str(), cam1Gray);
					cvSaveImage(imageNameRight.c_str(), cam2Gray);
					file << imageNameLeft;
					file << "\n";
					file << imageNameRight;
					file << "\n";
					std::cout << "saving " << imageNameLeft << std::endl;
					Sleep(10);
				}
				++count;
			}
			if (count >= 15)
			{
				std::cout << "Done!"<<std::endl;
				break;
			}
			cvShowImage("Cam1", cam1Resize);
			cvShowImage("Cam2", cam2Resize);
		}
		else
			break;
		key = cvWaitKey(1);
	}
	file.close();
	std::cout << "out of saving calib images"<<std::endl;
	cvDestroyWindow("Cam1");
	cvDestroyWindow("Cam2");
}

bool DualCamManager::checkFileQuality(FILE * f)
{
	if (f)
	{
		char * buff = (char*)malloc(sizeof(char*) * 1024);
		for (int i = 0 ; i < 1024 - 1; ++i)
			buff[i] = 0;
		fgets(buff, 1024 - 3, f);
		
		if (buff[0] != 'I')
		{
			fclose(f);
			_unlink(fileName.c_str());
			return false;//bad
		}
		return true;//good
	}
	return false;
}

bool DualCamManager::calibrate(int nx, int ny, int useUncalibrated, float squareSize)
{
	std::cout << "In calibrate()"<<std::endl;
	FILE * f = fopen(fileName.c_str(), "rt");
	if (!f)
	{
		saveCalibrationImages(nx, ny);
		std::cout << fileName << std::endl;
		FILE * f2 = fopen(fileName.c_str(), "rt");
		if (checkFileQuality(f2))
		{
			fclose(f2);
			bool res = false;
			res = calibManager->runCalibration();
			while (res == false)
			{
				if (res == false)
				{
					std::cout << "Average calibration error is > 2.0, would you like to recalibrate ?" << std::endl;
					std::cout << "Y:yes N:no"<<std::endl;
					std::string str;
					std::cin >> str;
					if (str == "Y" || str == "y" || str == "yes")
					{
						saveCalibrationImages(nx, ny);
						res = calibManager->runCalibration();
					}
					else
						break;
				}
				else
					break;
			}
		}
		//else
		//	return false;
	}
	else
	{
		iscalibrated = true;
		//calibManager = new CalibrationManager();
	}
	return true;
}

void DualCamManager::startStream(int camId1, int camId2)//be careful to modify due to use of directshow !
{
	cap1 = cvCreateCameraCapture(this->camId2);
	cap2 = cvCreateCameraCapture(this->camId1);
}

void DualCamManager::findVideoDevices()
{
	HRESULT hr;
	ICreateDevEnum *pSysDevEnum = NULL;
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
		IID_ICreateDevEnum, (void **)&pSysDevEnum);
	if (FAILED(hr))
	{
		return;
	}

	// Obtain a class enumerator for the video compressor category.
	IEnumMoniker *pEnumCat = NULL;
	hr = pSysDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnumCat, 0);

	if (hr == S_OK) 
	{
		// Enumerate the monikers.
		IMoniker *pMoniker = NULL;
		ULONG cFetched;
		while(pEnumCat->Next(1, &pMoniker, &cFetched) == S_OK)
		{
			IPropertyBag *pPropBag;
			hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, 
				(void **)&pPropBag);
			if (SUCCEEDED(hr))
			{
				// To retrieve the filter's friendly name, do the following:
				VARIANT varName;
				VariantInit(&varName);
				hr = pPropBag->Read(L"FriendlyName", &varName, 0);

				if (SUCCEEDED(hr))
				{
					std::string friendlyName;
					friendlyName.resize(50);
					BSTR ptr = varName.bstrVal;

					//std::cout << varName.pdispVal << std::endl;
					for (int c = 0; *ptr ; ++c, ++ptr)
					{
						friendlyName[c] = *ptr & 0xff;
					}
					deviceList.push_back(friendlyName);
				}
				VariantClear(&varName);

				// To create an instance of the filter, do the following:
				IBaseFilter *pFilter;
				hr = pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter,
					(void**)&pFilter);
				// Now add the filter to the graph. 
				//Remember to release pFilter later.
				pPropBag->Release();
			}
			pMoniker->Release();
		}
		pEnumCat->Release();
	}
	pSysDevEnum->Release();
	bool found = false;
	int i = 0, j = 0;
	for (i = 0 ; i < (int)deviceList.size() ; ++i)
	{
		found = false;
		for (j = 0 ; j < (int)deviceList.size() ; ++j)
		{
			found = false;
			if (deviceList[i] == deviceList[j] && i != j)
			{
				found = true;
				break;
			}
		}
		if (found = true)
			break;
	}
	camId1 = i;
	camId2 = j;
}

void DualCamManager::runThread()
{
	dualCamThread = new std::thread(&DualCamManager::run, this);
	threadRunning = true;
}

void DualCamManager::display3D()
{
	sf::Window App(sf::VideoMode(800, 600, 32), "Kinect Depth Stream");
	glClearDepth(1.f);
	glClearColor(0.f, 0.f, 0.f, 0.f);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	int X = 0, Y = 0, Z = -500;
	int rotateX = 0, rotateY = 0, rotateZ = 0;
	gluPerspective(45, 1.f, 1.f, 10000.f);
	gluLookAt(X, Y, Z, 0, 0, 0, 0, 1, 0);
	bool running = true;
	IplImage * temp = cvCreateImage(SizeCam1, IPL_DEPTH_32F, 3);
	while (!stop3D)
	{
				sf::Event Event;

		if (App.pollEvent(Event))
		{
			if (Event.key.code == sf::Keyboard::Escape)
			{
				stop3D = true;
			}
				if (Event.key.code == sf::Keyboard::W)
					{
						Z -= 10;
					}
					if (Event.key.code == sf::Keyboard::S)
					{
						Z += 10;
					}
					if (Event.key.code == sf::Keyboard::A)
					{
						X -= 10;
					}
					if (Event.key.code == sf::Keyboard::D)
					{
						X += 10;
					}
					if (Event.key.code == sf::Keyboard::Q)
					{
						Y -= 10;
					}
					if (Event.key.code == sf::Keyboard::E)
					{
						Y += 10;
					}	if (Event.key.code == sf::Keyboard::I)
					{
						rotateX += 1;
					}
					if (Event.key.code == sf::Keyboard::K)
					{
						rotateX -= 1;
					}
					if (Event.key.code == sf::Keyboard::I)
					{
						rotateX += 1;
					}
					if (Event.key.code == sf::Keyboard::J)
					{
						rotateY -= 1;
					}
					if (Event.key.code == sf::Keyboard::L)
					{
						rotateY += 1;
					}
					if (Event.key.code == sf::Keyboard::U)
					{
						rotateZ -= 1;
					}
					if (Event.key.code == sf::Keyboard::O)
					{
						rotateZ += 1;
					}
					if (Event.key.code == sf::Keyboard::R)
					{
						rotateX = 0;
						rotateY = 0;
						rotateZ = 0;
						X = 0, Y = 0, Z = -500;
					}
		}

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		//glRotatef(180, 0, 1, 0);
		//glRotatef(rotateX, 1, 0, 0);
		//glRotatef(rotateY, 0, 1, 0);
		//glRotatef(rotateZ, 0, 0, 1);
		//glTranslatef(X, Y, Z);
		glRotatef(180, 0, 1, 0);
		glRotatef(rotateX, 1, 0, 0);
		glRotatef(rotateY, 0, 1, 0);
		glRotatef(rotateZ, 0, 0, 1);
		glTranslatef(X, Y, Z);
		glPointSize(1);
		glBegin(GL_POINTS);

	
		//if (depthUpdated)
		//{
			//cvCopy(depthData, temp);

			for (int j = 0 ; j < SizeCam1.height ; ++j)
			{
				for (int i = 0 ; i < SizeCam1.width ; ++i)
				{
					CvScalar d;
					glColor3ub(cam1->imageData[(j * 3) * SizeCam1.width + (i * 3)], cam1->imageData[(j * 3) * SizeCam1.width + (i * 3) + 1], cam1->imageData[(j * 3) * SizeCam1.width + (i * 3) + 2]);
					// d = ccvGet2D(depthData,i,j);
					//glVertex3f(depthData->imageData[(j * 3) * SizeCam1.width + (i * 3)], depthData->imageData[(j * 3) * SizeCam1.width + (i * 3) + 1], depthData->imageData[(j * 3) * SizeCam1.width + (i * 3) + 2]);
					//glVertex3f(i, j, (double)depthData->data.db[j * SizeCam1.width + i]);
				}

			}
		glEnd();
		App.display();
	}
	App.close();
	cvReleaseImage(&temp);
}

#define X 640
#define Y 480

void DualCamManager::run()
{
	int k = -1;
	std::ofstream f;
	IplImage * undist1 = cvCreateImage(SizeCam1, 8, 1);
	IplImage * undist2 = cvCreateImage(SizeCam2, 8, 1);
	CvMat * disp = cvCreateMat(SizeCam1.height,  SizeCam1.width, CV_16SC1);
	CvMat * vdisp = cvCreateMat(SizeCam2.height,  SizeCam2.width, CV_8U);
	IplImage * vdispConv = cvCreateImage(SizeCam1, 8, 3);
	IplImage* real_disparity= cvCreateImage(SizeCam1, IPL_DEPTH_8U, 1 ); 
	CvMat*_3d = cvCreateMat(SizeCam1.height, SizeCam1.width, CV_32FC3);
	calibManager->loadData(cvGetSize(undist1));
	IplImage* depth = cvCreateImage(SizeCam1, IPL_DEPTH_32F, 3 );
	//depthData = cvCreateImage(SizeCam1, IPL_DEPTH_32F, 3);
	depthData = cvCreateMat(SizeCam1.height, SizeCam1.width, IPL_DEPTH_32F);
	double scaleit = 255.0 / (0.0 - (-3000.0));
	stop3D = false;
	display3DThread = new std::thread(&DualCamManager::display3D, this);
	//double _Q[] = { 1., 0., 0., -3.2554532241821289e+002, 0., 1., 0.,
 //      -2.4126087760925293e+002, 0., 0., 0., 4.2440051858596559e+002, 0.,
 //          0., -2.9937622423123672e-001, 0. }; 
	depthUpdated = false;
	 f.open("data.txt");
	 cvNamedWindow("disp");
	 CvMat * depthMat = cvCreateMat(480, 640, IPL_DEPTH_32F);
	while (k != 27)
	{
		cam1 = cvQueryFrame(cap1);
		cam2 = cvQueryFrame(cap2);
		depthUpdated = false;
		switch (k)
		{
		case 's' :
			calibManager->BMState->minDisparity++;
			std::cout << "MinDisparity : "<< calibManager->BMState->minDisparity << std::endl;
			break;
		case 'w' :
			calibManager->BMState->minDisparity--;
			std::cout << "MinDisparity : " << calibManager->BMState->minDisparity << std::endl;
			break;
		case '1':
			if (calibManager->BMState->SADWindowSize + 2 < 255)
			{
				calibManager->BMState->SADWindowSize+=2;
			}
			std::cout << "SADWindow : " << calibManager->BMState->SADWindowSize << std::endl;
			break;
		case '2':
			if (calibManager->BMState->SADWindowSize - 2 > 5)
				calibManager->BMState->SADWindowSize-=2;
			std::cout << "SADWindow : " << calibManager->BMState->SADWindowSize << std::endl;
			break;
		}
		if (cam1 && cam2)
		{
			cvResize(cam1, cam1Resize, CV_INTER_CUBIC);

			cvResize(cam2, cam2Resize, CV_INTER_CUBIC);
			cvCvtColor(cam1Resize, cam1Gray, CV_RGB2GRAY);
			cvCvtColor(cam2Resize, cam2Gray, CV_RGB2GRAY);

			cvRemap(cam1Gray, undist1, calibManager->mx1, calibManager->my1);
			cvRemap(cam2Gray, undist2, calibManager->mx2, calibManager->my2);
			
			cvFindStereoCorrespondenceBM(undist1, undist2, disp, calibManager->BMState);
			cvNormalize(disp, vdisp, 0, 256, CV_MINMAX);
			//cvConvertScale(vdisp, real_disparity, 1.0/16, 0 );
			
			cvReprojectImageTo3D(vdisp, depthMat, calibManager->_Q2, 0);

			cvCopy(depthMat, depthData);
			depthUpdated = true;
			//cvSave("image.txt", depth);
			//for (int j = 0 ; j < X; ++j)
			//{
			//	for (int i = 0; i < Y ; ++i)
			//	{
			//		depth->imageData[(j * depth->nChannels) * X + (i * depth->nChannels)] = 0;
			//		depth->imageData[(j * depth->nChannels) * X + (i * depth->nChannels) + 1] = 0;
			//		depth->imageData[(j * depth->nChannels) * X + (i * depth->nChannels) + 2] = 0;
			//	}
			//}
			//cvShowImage("cam2", cam2);
			cvShowImage("disp", vdisp);
			//cvShowImage("_3d", depth);
		}
		if (stopThread == true)
			break;
		k = cvWaitKey(1);
	}
	stop3D = true;
	cvDestroyAllWindows();
	cvReleaseImage(&undist1);
	cvReleaseImage(&undist2);
	cvReleaseMat(&disp);
	cvReleaseMat(&vdisp);
	cvReleaseImage(&vdispConv);
	cvReleaseImage(&real_disparity);
	cvReleaseMat(&_3d);
	cvReleaseImage(&depth);
	f.close();
}