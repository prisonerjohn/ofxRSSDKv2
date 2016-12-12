#include <exception>
#include "ofxRSSDKv2.h"
#include "ofMain.h"

namespace ofxRSSDK
{
	RSDevice::~RSDevice(){}
	RSDevice::RSDevice()
	{
		mIsInit = false;
		mIsRunning = false;
		mHasRgb = false;
		mHasDepth = false;
		mShouldAlign = false;
		mShouldGetDepthAsColor = false;
		mShouldGetPointCloud = false;
		mShouldGetFaces = false;
		mShouldGetBlobs = false;
		mPointCloudRange = ofVec2f(0,3000);
		mCloudRes = CloudRes::FULL_RES;

		bLittleEndian = isLittleEndian(); // --> BGR vs RGB channel order
	}

#pragma region Init
	bool RSDevice::init()
	{
		mSenseMgr = PXCSenseManager::CreateInstance();

		if (mSenseMgr)
			mIsInit = true;

		return mIsInit;
	}
	// use this init function to set a depth range
	// not working right now - at least not with SR300
	/*
	bool RSDevice::init(float nearclip, float farclip)
	{
		session = PXCSession::CreateInstance();
		desc1 = {};
		desc1.group = PXCSession::IMPL_GROUP_SENSOR;
		desc1.subgroup = PXCSession::IMPL_SUBGROUP_VIDEO_CAPTURE; // ?
		
		session->CreateImpl<PXCCapture>(&desc1, &c);
		device = c->CreateDevice(0);
		if(!device)
			return mIsInit;
		depthRange.min = nearclip;
		depthRange.max = farclip;
		pxcStatus st = device->SetDSMinMaxZ(depthRange);
		if (st < 0) {
			ofLogError("RSDevice::init") << "couldn't set depth range of device";
		}
		mSenseMgr = session->CreateSenseManager();

		if (mSenseMgr)
			mIsInit = true;

		return mIsInit;
	}
	*/

	bool RSDevice::initRgb(const RGBRes& pSize, const float& pFPS)
	{
		pxcStatus cStatus;
		if (mSenseMgr)
		{
			if (pSize != RGBRes::ANY_RGBRES) 
			{
				switch (pSize)
				{
				case RGBRes::VGA:
					mRgbSize = ofVec2f(640, 480);
					break;
				case RGBRes::HD720:
					mRgbSize = ofVec2f(1280, 720);
					break;
				case RGBRes::HD1080:
					mRgbSize = ofVec2f(1920, 1080);
					break;
				}

				if (pFPS != 0.f)
					cStatus = mSenseMgr->EnableStream(PXCCapture::STREAM_TYPE_COLOR, mRgbSize.x, mRgbSize.y, pFPS);
				else
					cStatus = mSenseMgr->EnableStream(PXCCapture::STREAM_TYPE_COLOR, mRgbSize.x, mRgbSize.y);
			}
			else 
			{
				if (pFPS != 0.f)
					cStatus = mSenseMgr->EnableStream(PXCCapture::STREAM_TYPE_COLOR, 0, 0, pFPS);
				else
					cStatus = mSenseMgr->EnableStream(PXCCapture::STREAM_TYPE_COLOR);
			}
			if (cStatus >= PXC_STATUS_NO_ERROR)
			{
				mHasRgb = true;
				//ofPixelFormat pxF = bLittleEndian ? OF_PIXELS_BGR : OF_PIXELS_RGB;
				//mRgbFrame.allocate(mRgbSize.x, mRgbSize.y, pxF);
			}
		}

		return mHasRgb;
	}

	bool RSDevice::initDepth(const DepthRes& pSize, const float& pFPS, bool pAsColor)
	{
		pxcStatus cStatus;
		if (mSenseMgr)
		{
			switch (pSize)
			{
			case DepthRes::R200_SD:
				mDepthSize = ofVec2f(480,360);
				break;
			case DepthRes::R200_VGA:
				mDepthSize = ofVec2f(628,468);
				break;
			case DepthRes::F200_VGA:
				mDepthSize = ofVec2f(640,480);
				break;
			case DepthRes::QVGA:
				mDepthSize = ofVec2f(320,240);
				break;
			}
			cStatus = mSenseMgr->EnableStream(PXCCapture::STREAM_TYPE_DEPTH, mDepthSize.x, mDepthSize.y, pFPS);
			if (cStatus >= PXC_STATUS_NO_ERROR)
			{
				mHasDepth = true;
				mShouldGetDepthAsColor = pAsColor;
				mDepthFrame.allocate(mDepthSize.x, mDepthSize.y, OF_PIXELS_GRAY);
				ofPixelFormat pxF = bLittleEndian ? OF_PIXELS_BGRA : OF_PIXELS_RGBA;
				mDepth8uFrame.allocate(mDepthSize.x, mDepthSize.y, pxF);
				mRawDepth = new uint16_t[(int)mDepthSize.x*(int)mDepthSize.y]; // ? isn't this duplicate of mDepthFrame (uses ofShortPixels) ?
			}
		}
		return mHasDepth;
	}
#pragma endregion

	void RSDevice::setPointCloudRange(float pMin=100.0f, float pMax=1500.0f)
	{
		mPointCloudRange = ofVec2f(pMin,pMax);
	}

	bool RSDevice::start()
	{
		pxcStatus cStatus = mSenseMgr->Init();
		if (cStatus >= PXC_STATUS_NO_ERROR)
		{
			mCoordinateMapper = mSenseMgr->QueryCaptureManager()->QueryDevice()->CreateProjection();
			if (mShouldAlign)
			{
				ofPixelFormat pxF = bLittleEndian ? OF_PIXELS_BGRA : OF_PIXELS_RGBA;
				mColorToDepthFrame.allocate(mRgbSize.x, mRgbSize.y, pxF);
				mDepthToColorFrame.allocate(mRgbSize.x, mRgbSize.y, pxF);
			}
			mIsRunning = true;
			return true;
		}
		return false;
	}

	bool RSDevice::update()
	{
		pxcStatus cStatus;
		if (mSenseMgr)
		{
			// check new frame
			cStatus = mSenseMgr->AcquireFrame(true,0);
			if (cStatus < PXC_STATUS_NO_ERROR)
				return false;

			// faces
			if (mShouldGetFaces) {
				updateFaces();
			}

			PXCCapture::Sample *mCurrentSample = mSenseMgr->QuerySample();
			if (!mCurrentSample)
				return false;

			// color
			if (mHasRgb)
			{
				if (!mCurrentSample->color)
					return false; // ? why not continue...?
				PXCImage *cColorImage = mCurrentSample->color;
				PXCImage::ImageData cColorData;
				cStatus = cColorImage->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_RGB24, &cColorData);
				if (cStatus < PXC_STATUS_NO_ERROR)
				{
					cColorImage->ReleaseAccess(&cColorData);
					return false;
				}
				ofPixelFormat pxF = bLittleEndian ? OF_PIXELS_BGR : OF_PIXELS_RGB;
				mRgbFrame.setFromExternalPixels(reinterpret_cast<uint8_t *>(cColorData.planes[0]), mRgbSize.x, mRgbSize.y, pxF);
				// memory leak?  shouldn't this be setFromPixels()?  shouldn't be reallocated anyway

				cColorImage->ReleaseAccess(&cColorData);
				if (!mHasDepth)
				{	
					mSenseMgr->ReleaseFrame();
					return true;
				}
			}

			// depth
			if (mHasDepth)
			{
				if (!mCurrentSample->depth)
					return false;
				PXCImage *cDepthImage = mCurrentSample->depth;
				PXCImage::ImageData cDepthData;
				cStatus = cDepthImage->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_DEPTH, &cDepthData);
				
				if (cStatus < PXC_STATUS_NO_ERROR)
				{
					cDepthImage->ReleaseAccess(&cDepthData);
					return false;
				}
				mDepthFrame.setFromExternalPixels(reinterpret_cast<uint16_t *>(cDepthData.planes[0]), mDepthSize.x, mDepthSize.y, OF_PIXELS_GRAY);
				memcpy(mRawDepth, reinterpret_cast<uint16_t *>(cDepthData.planes[0]), (size_t)((int)mDepthSize.x*(int)mDepthSize.y*sizeof(uint16_t)));			
				cDepthImage->ReleaseAccess(&cDepthData);

				if (mShouldGetDepthAsColor)
				{
					PXCImage::ImageData cDepth8uData;
					cStatus = cDepthImage->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_RGB32, &cDepth8uData); // ? why not use 8 bit gray ?
					if (cStatus < PXC_STATUS_NO_ERROR)
					{
						cDepthImage->ReleaseAccess(&cDepth8uData);
						return false;
					}
					ofPixelFormat pxF = bLittleEndian ? OF_PIXELS_BGRA : OF_PIXELS_RGBA;
					mDepth8uFrame.setFromExternalPixels(reinterpret_cast<uint8_t *>(cDepth8uData.planes[0]), mDepthSize.x, mDepthSize.y, pxF);
					cDepthImage->ReleaseAccess(&cDepth8uData);
				}

				if(mShouldGetPointCloud)
				{
					updatePointCloud();
				}

				if (!mHasRgb)
				{
					mSenseMgr->ReleaseFrame();
					return true;
				}
			}

			// mapped color<-->depth imgs
			if (mHasDepth&&mHasRgb&&mShouldAlign&&mAlignMode==AlignMode::ALIGN_FRAME)
			{
				PXCImage *cMappedColor = mCoordinateMapper->CreateColorImageMappedToDepth(mCurrentSample->depth, mCurrentSample->color);
				PXCImage *cMappedDepth = mCoordinateMapper->CreateDepthImageMappedToColor(mCurrentSample->color, mCurrentSample->depth);

				if (!cMappedColor || !cMappedDepth)
					return false;

				PXCImage::ImageData cMappedColorData;
				if (cMappedColor->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_RGB32, &cMappedColorData) >= PXC_STATUS_NO_ERROR)
				{
					mColorToDepthFrame.setFromExternalPixels(reinterpret_cast<uint8_t *>(cMappedColorData.planes[0]), mRgbSize.x, mRgbSize.y, 4);
					cMappedColor->ReleaseAccess(&cMappedColorData);
				}

				PXCImage::ImageData cMappedDepthData;
				if (cMappedDepth->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_RGB32, &cMappedDepthData) >= PXC_STATUS_NO_ERROR)
				{
					mDepthToColorFrame.setFromExternalPixels(reinterpret_cast<uint8_t *>(cMappedDepthData.planes[0]), mRgbSize.x, mRgbSize.y, 4);
					cMappedDepth->ReleaseAccess(&cMappedDepthData);
				}

				try
				{
					cMappedColor->Release();
					cMappedDepth->Release();
				}
				catch (const exception &e)
				{
					ofLog(ofLogLevel::OF_LOG_WARNING, "Release check error: ");
					ofLog(ofLogLevel::OF_LOG_WARNING, e.what());
				}
			}
			mSenseMgr->ReleaseFrame();
			return true;
		}
		return false;
	}

	bool RSDevice::stop()
	{
		if (mSenseMgr)
		{
			mCoordinateMapper->Release();

			if(mShouldGetBlobs)
			{
				if(mBlobTracker)
					mBlobTracker->Release();
			}
			if(mShouldGetFaces)
			{
				if (mFaceData)
					mFaceData->Release();
			}

			mSenseMgr->Close();

			return true;
		}
		delete [] mRawDepth;
		return false;
	}

#pragma region Enable
		bool RSDevice::enableFaceTracking(bool pUseDepth)
		{
			if(mSenseMgr)
			{
				if(mSenseMgr->EnableFace()>=PXC_STATUS_NO_ERROR)
				{
					mFaceTracker = mSenseMgr->QueryFace();
					if(mFaceTracker)
					{
						PXCFaceConfiguration *config = mFaceTracker->CreateActiveConfiguration();
						switch(pUseDepth)
						{
						case true:
							config->SetTrackingMode(PXCFaceConfiguration::TrackingModeType::FACE_MODE_COLOR_PLUS_DEPTH);
							break;
						case false:
							config->SetTrackingMode(PXCFaceConfiguration::TrackingModeType::FACE_MODE_COLOR);
							break;
						}
						config->ApplyChanges();
						config->Release();

						mFaceData = mFaceTracker->CreateOutput();

						mShouldGetFaces = true;
					}
					else
						mShouldGetFaces = false;
					return mShouldGetFaces;
				}
				return false;
			}
			return false;
		}

		bool RSDevice::enableBlobTracking()
		{
			if(mSenseMgr)
			{
				if(mSenseMgr->EnableBlob()>=PXC_STATUS_NO_ERROR)
				{
					mBlobTracker = mSenseMgr->QueryBlob();
					if(mBlobTracker)
						mShouldGetBlobs = true;
					else
						mShouldGetBlobs = false;
					return mShouldGetBlobs;
				}
				return false;
			}
			return false;

		}
#pragma endregion

#pragma region Update
	void RSDevice::updatePointCloud()
	{
		int width = (int)mDepthSize.x;
		int height = (int)mDepthSize.y;
		int step = (int)mCloudRes;
		mPointCloud.clear();
		vector<PXCPoint3DF32> depthPoints, worldPoints;
		for (int dy = 0; dy < height;dy+=step)
		{
			for (int dx = 0; dx < width; dx+=step)
			{
				PXCPoint3DF32 cPoint;
				cPoint.x = dx; cPoint.y = dy; cPoint.z = (float)mRawDepth[dy*width + dx];
				if(cPoint.z>mPointCloudRange.x&&cPoint.z<mPointCloudRange.y)
					depthPoints.push_back(cPoint);
			}
		}

		size_t nPts = depthPoints.size();
		if (nPts) {
			worldPoints.resize(nPts);
			mCoordinateMapper->ProjectDepthToCamera((pxcI32)depthPoints.size(), &depthPoints[0], &worldPoints[0]);

			mPointCloud.reserve(nPts);
		}

		for (int i = 0; i < depthPoints.size();++i)
		{
			PXCPoint3DF32 p = worldPoints[i];
			mPointCloud.push_back(ofVec3f(p.x, p.y, p.z));
		}
	}


	void RSDevice::updateFaces() {

		PXCFaceModule* face = mSenseMgr->QueryFace(); // get face module
		if (face) {

			// update face data
			if (mFaceData)
				mFaceData->Update();
			else
				ofLogError("ofxRSSDK") << "unable to update face data, null pointer";

			//PXCCapture::Sample* sample = mSenseMgr->QueryFaceSample();

			// num faces
			pxcI32 nFaces = mFaceData->QueryNumberOfDetectedFaces();

			mFaces.clear();
			mFaces.resize((size_t)nFaces);

			for (int i = 0; i < nFaces; i++){

				// get landmarks
				PXCFaceData::Face* face = mFaceData->QueryFaceByIndex(i);
				if (!face) continue;

				PXCFaceData::LandmarksData* landmarkData = face->QueryLandmarks();
				if (!landmarkData) continue;

				mFaces[i].resize((size_t)landmarkData->QueryNumPoints()); // allocate space for landmarks

				if (!landmarkData->QueryPoints(mFaces[i].data())) // fills face vector data with landmarks
					ofLogError("ofxRSSDK") << "unable to query landmarks for face " << i;
			}
		}

	}

#pragma endregion

#pragma region Getters
	const ofPixels& RSDevice::getRgbFrame()
	{
		return mRgbFrame;
	}

	const ofShortPixels& RSDevice::getDepthFrame()
	{
		return mDepthFrame;
	}

	const ofPixels& RSDevice::getDepth8uFrame()
	{
		return mDepth8uFrame;
	}

	const ofPixels& RSDevice::getColorMappedToDepthFrame()
	{
		return mColorToDepthFrame;
	}

	const ofPixels& RSDevice::getDepthMappedToColorFrame()
	{
		return mDepthToColorFrame;
	}


	vector<ofVec3f> RSDevice::getPointCloud()
	{
		return mPointCloud;
	}

	//Nomenclature Notes:
	//	"Space" denotes a 3d coordinate
	//	"Image" denotes an image space point ((0, width), (0,height), (image depth))
	//	"Coords" denotes texture space (U,V) coordinates
	//  "Frame" denotes a full Surface

	//get a camera space point from a depth image point
	const ofPoint RSDevice::getDepthSpacePoint(float pImageX, float pImageY, float pImageZ)
	{
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pImageX;
			cPoint.y = pImageY;
			cPoint.z = pImageZ;

			mInPoints3D.clear();
			mInPoints3D.push_back(cPoint);
			mOutPoints3D.clear();
			mOutPoints3D.resize(2);
			mCoordinateMapper->ProjectDepthToCamera(1, &mInPoints3D[0], &mOutPoints3D[0]);
			return ofPoint(mOutPoints3D[0].x, mOutPoints3D[0].y, mOutPoints3D[0].z);
		}
		return ofPoint(0);
	}

	const ofPoint RSDevice::getDepthSpacePoint(int pImageX, int pImageY, uint16_t pImageZ)
	{
		return getDepthSpacePoint(static_cast<float>(pImageX), static_cast<float>(pImageY), static_cast<float>(pImageZ));
	}

	const ofPoint RSDevice::getDepthSpacePoint(ofPoint pImageCoords)
	{
		return getDepthSpacePoint(pImageCoords.x, pImageCoords.y, pImageCoords.z);
	}

	//get a Color object from a depth image point
	const ofColor RSDevice::getColorFromDepthImage(float pImageX, float pImageY, float pImageZ)
	{
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pImageX;
			cPoint.y = pImageY;
			cPoint.z = pImageZ;
			PXCPoint3DF32 *cInPoint = new PXCPoint3DF32[1];
			cInPoint[0] = cPoint;
			PXCPointF32 *cOutPoints = new PXCPointF32[1];
			mCoordinateMapper->MapDepthToColor(1, cInPoint, cOutPoints);

			float cColorX = cOutPoints[0].x;
			float cColorY = cOutPoints[0].y;

			delete cInPoint;
			delete cOutPoints;
			if (cColorX >= 0 && cColorX < mRgbSize.x&&cColorY >= 0 && cColorY < mRgbSize.y)
			{
				return mRgbFrame.getColor(cColorX, cColorY);
			}
		}
		return ofColor::black;
	}

	const ofColor RSDevice::getColorFromDepthImage(int pImageX, int pImageY, uint16_t pImageZ)
	{
		if (mCoordinateMapper)
			return getColorFromDepthImage(static_cast<float>(pImageX),static_cast<float>(pImageY),static_cast<float>(pImageZ));
		return ofColor::black;
	}

	const ofColor RSDevice::getColorFromDepthImage(ofPoint pImageCoords)
	{
		if (mCoordinateMapper)
			return getColorFromDepthImage(pImageCoords.x, pImageCoords.y, pImageCoords.z);
		return ofColor::black;
	}


		//get a ofColor object from a depth camera space point
	const ofColor RSDevice::getColorFromDepthSpace(float pCameraX, float pCameraY, float pCameraZ)
	{
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pCameraX; cPoint.y = pCameraY; cPoint.z = pCameraZ;

			mInPoints3D.clear();
			mInPoints3D.push_back(cPoint);
			mOutPoints2D.clear();
			mOutPoints2D.resize(2);
			mCoordinateMapper->ProjectCameraToColor(1, &mInPoints3D[0], &mOutPoints2D[0]);

			int imageX = static_cast<int>(mOutPoints2D[0].x);
			int imageY = static_cast<int>(mOutPoints2D[0].y);
			if( (imageX>=0&&imageX<mRgbSize.x)  &&(imageY>=0&&imageY<mRgbSize.y))
				return mRgbFrame.getColor(imageX, imageY);
			return ofColor::black;
		}
		return ofColor::black;
	}

	const ofColor RSDevice::getColorFromDepthSpace(ofPoint pCameraPoint)
	{
		if (mCoordinateMapper)
			return getColorFromDepthSpace(pCameraPoint.x, pCameraPoint.y, pCameraPoint.z);
		return ofColor::black;
	}

		//get ofColor space UVs from a depth image point
	const ofVec2f RSDevice::getColorCoordsFromDepthImage(float pImageX, float pImageY, float pImageZ)
	{
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pImageX;
			cPoint.y = pImageY;
			cPoint.z = pImageZ;

			PXCPoint3DF32 *cInPoint = new PXCPoint3DF32[1];
			cInPoint[0] = cPoint;
			PXCPointF32 *cOutPoints = new PXCPointF32[1];
			mCoordinateMapper->MapDepthToColor(1, cInPoint, cOutPoints);

			float cColorX = cOutPoints[0].x;
			float cColorY = cOutPoints[0].y;

			delete cInPoint;
			delete cOutPoints;
			return ofVec2f(cColorX / (float)mRgbSize.x, cColorY / (float)mRgbSize.y);
		}
		return ofVec2f(0);
	}

	const ofVec2f RSDevice::getColorCoordsFromDepthImage(int pImageX, int pImageY, uint16_t pImageZ)
	{
		return getColorCoordsFromDepthImage(static_cast<float>(pImageX), static_cast<float>(pImageY), static_cast<float>(pImageZ));
	}

	const ofVec2f RSDevice::getColorCoordsFromDepthImage(ofPoint pImageCoords)
	{
		return getColorCoordsFromDepthImage(pImageCoords.x, pImageCoords.y, pImageCoords.z);
	}

		//get ofColor space UVs from a depth space point
	const ofVec2f RSDevice::getColorCoordsFromDepthSpace(float pCameraX, float pCameraY, float pCameraZ)
	{
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pCameraX; cPoint.y = pCameraY; cPoint.z = pCameraZ;

			PXCPoint3DF32 *cInPoint = new PXCPoint3DF32[1];
			cInPoint[0] = cPoint;
			PXCPointF32 *cOutPoint = new PXCPointF32[1];
			mCoordinateMapper->ProjectCameraToColor(1, cInPoint, cOutPoint);

			ofVec2f cRetPt(cOutPoint[0].x / static_cast<float>(mRgbSize.x), cOutPoint[0].y / static_cast<float>(mRgbSize.y));
			delete cInPoint;
			delete cOutPoint;
			return cRetPt;
		}
		return ofVec2f(0);
	}

	const ofVec2f RSDevice::getColorCoordsFromDepthSpace(ofPoint pCameraPoint)
	{
		return getColorCoordsFromDepthSpace(pCameraPoint.x, pCameraPoint.y, pCameraPoint.z);
	}
}
#pragma endregion