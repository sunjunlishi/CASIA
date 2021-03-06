/*
Author:Michael Zhu.
QQ:1265390626.
Email: 1265390626@qq.com
QQ群: 279441740
GIT: https://github.com/michael-zhu-sh/CASIA
本项目使用OpenCV提供的HOG特征和SVM分类器，
在CASIA HWDB手写汉字数据集上，进行了训练，完成了2013年中文手写识别大赛的第2个任务。
本项目的离线手写汉字训练数据集下载链接http://www.nlpr.ia.ac.cn/databases/download/feature_data/HWDB1.1trn_gnt.zip
测试数据集下载链接http://www.nlpr.ia.ac.cn/databases/download/feature_data/HWDB1.1tst_gnt.zip
ICDAR2013大赛官网http://www.nlpr.ia.ac.cn/events/CHRcompetition2013/competition/Home.html
大赛测试数据集下载链接http://www.nlpr.ia.ac.cn/databases/Download/competition/competition-gnt.zip
 */
#include "stdafx.h"
#include <fstream>
#include <iostream>
#include <io.h>
#include <sstream>
#include <algorithm>
#include <vector>

#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;
using namespace cv::ml;

//GNT文件中每个汉字的头信息。
struct GNT_HEADER {
	unsigned int sampleSize;
	unsigned char tagCode[2];
	unsigned short width;
	unsigned short height;
};

const string TRAINSET_DIR = "/MLDataset/CASIA/HWDB1.1/train";	//训练集目录。
const string TESTSET_DIR = "/MLDataset/CASIA/HWDB1.1/test";		//测试集目录。
const string ICDAR2013_DIR = "/MLDataset/CASIA/HWDB1.1/competition";	//大赛测试集目录。
uchar fileBuf[1024 * 30];	//汉字图像缓存，每个汉字大小不同。
const int GNT_HEADER_SIZE = 10;
/*全局HOG参数--------------------*/
/*
const cv::Size winSize(70, 77);
const cv::Size blockSize(14, 14);
const cv::Size blockStride(7, 7);
const cv::Size cellSize(7, 7);
const int FEATURE_DIMENSION = 3240;	//一个汉字的特征向量维度。
*/
/*全局HOG参数--------------------*/
const cv::Size winSize(64, 64);	//原始汉字从文件读入后，需要尺度转换到这里64cols64rows。
const cv::Size blockSize(16, 16);
const cv::Size blockStride(8, 8);
const cv::Size cellSize(8, 8);
const int nbins = 9;
const int FEATURE_DIMENSION = 1764;
/*全局HOG参数--------------------*/
/*
const Size winSize(28, 28);
const Size blockSize(14, 14);
const Size blockStride(7, 7);
const Size cellSize(7, 7);
const int nbins = 9;
const int FEATURE_DIMENSION = 324;
/*全局HOG参数--------------------*/

Mat trainFeatureMat(0, FEATURE_DIMENSION, CV_32FC1);//存放训练集所有的HOG特征向量。
Mat trainLabelMat(0, 1, CV_32SC1);

cv::Ptr<SVM> linearSvmPtr;
cv::Ptr<SVM> rbfSvmPtr;

/*
把tagCode[2]转换成int。
*/
int readChineseFromTagCode(const GNT_HEADER &header) {
	unsigned short tag, ltag, rtag;
	tag = header.tagCode[0];
	ltag = tag << 8;
	rtag = header.tagCode[1];
	return (ltag | rtag);
}

int getTagCodeFromInt(const int label, unsigned char *tagCode) {
	unsigned short tag;
	tag = static_cast<unsigned short>(label);
	tagCode[0] = static_cast<unsigned char>(tag>>8);
	tagCode[1] = static_cast<unsigned char>((tag<<8)>>8);

	return 0;
}

/*
计算HOG特征矩阵的维数。
*/
unsigned short computeHogFeatureDimension(const Size imageSize, const Size &winSize, const Size &blockSize,
	const Size &blockStride, const Size &cellSize, const unsigned short nbins) {
	
	return	(1+(imageSize.width-blockSize.width)/blockStride.width)*
			(1+(imageSize.height-blockSize.height)/blockStride.height)*
			(blockSize.width/cellSize.width)*
			(blockSize.height/cellSize.height)*
			nbins;
}

/*
从1个GNT文件中解析汉字图像和编码。
参数：
miniBatchLabels:如果它的size()不为0，则在该容器中的汉字图像才会进入retImages中被返回。
返回：
retImages:存放解析出来的汉字图像。
retLabels:存放汉字图像对应的GB码。
*/
int readAGnt(const string &gntFilepath, vector<Mat> &retImages, vector<int> &retLabels, const vector<int> &miniBatchLabels=vector<int>()) {
	GNT_HEADER header;
	stringstream ss;
	string filename;
	int sampleCnt = 0, imgSize = 0, label;
	int rowSum = 0;// rowMean;	//计算汉字样本图像的行数均值(78)。
	int colSum = 0;// colMean;	//计算汉字样本图像的列数均值(67)。
	vector<int>::const_iterator it;
	ifstream is(gntFilepath, ios::in | ios::binary);
	if (!is)
	{
		cerr << "FAIL to open input GNT file!" << endl;
		return 1;
	}
	else {
		cout << "Succeed to open input GNT file." << endl;
	}
	is.read((char*)&header, GNT_HEADER_SIZE);	//读取该GNT的第1个汉字。
	while (is) {
		imgSize = header.width * header.height;
		is.read((char*)fileBuf, imgSize);
		if (!is) {
			cerr << "error: only " << is.gcount() << " could be read" << endl;
			break;
		}
		else {
			rowSum += header.height;
			colSum += header.width;
			sampleCnt++;
		}
		label = readChineseFromTagCode(header);
		if (0 != miniBatchLabels.size()) {
			it = find(miniBatchLabels.begin(), miniBatchLabels.end(), label);
			if (it == miniBatchLabels.end()) {
				is.read((char*)&header, GNT_HEADER_SIZE);	//继续读取GNT中的后续汉字。
				continue;
			}
		}
		Mat originalImg = (Mat_<uchar>(header.height, header.width, fileBuf));	//GNT中的原始汉字图像，可能需要【图像预处理】。
		Mat resizeImg;
		cv::resize(originalImg, resizeImg, winSize);	//尺度变换到HOG计算。
		retImages.push_back(resizeImg);
		retLabels.push_back(label);
		originalImg.release();
		resizeImg.release();
		/*
		Mat thresImg;
		cv::threshold(cImg, thresImg, 130, 255, THRESH_BINARY_INV);
		*/
		/*
		ss<<sampleCnt++;
		ss>>filename;
		filename = "output/trainsample" + filename + ".png";
		imwrite(filename,	resizeImg);
		if (sampleCnt>10)	break;
		ss.clear();
		*/

		is.read((char*)&header, GNT_HEADER_SIZE);	//继续读取GNT中的后续汉字。
	}

	is.close();
	/*
	rowMean = rowSum / sampleCnt;
	colMean = colSum / sampleCnt;
	cout << "Succeed to read " << sampleCnt << " CHINESE samples from GNT " << gntFilepath << ", row mean:" << rowMean << ", col mean:" << colMean << endl;
	*/
	return 0;
}

int computeAHog(const Mat &image, Mat &featureMat) {
	cv::HOGDescriptor hog(winSize, blockSize, blockStride, cellSize, nbins);
	vector<float> descriptors;
	hog.compute(image, descriptors, Size(1, 1));
	Mat tmp = Mat(descriptors, true).t();
	tmp.row(0).copyTo(featureMat.row(0));	//保存HOG特征向量。

	return 0;
}

/*
计算images中汉字图像的HOG特征。
*/
int computeHog(const vector<Mat> &images, Mat &trainFeatureMat) {
	const size_t imgCnt = images.size();
	cv::HOGDescriptor hog(winSize, blockSize, blockStride, cellSize, nbins);
	vector<float> descriptors;
	for (int i = 0; i != imgCnt; i++) {
		hog.compute(images[i], descriptors, Size(1, 1));
		Mat tmp = Mat(descriptors, true).t();
		tmp.row(0).copyTo(trainFeatureMat.row(i));	//保存HOG特征向量。
		tmp.release();
		descriptors.clear();
	}

	return 0;
}

/*
找到指定目录下所有的文件。
*/
void getAllFiles(const string &path, vector<string> &files)
{
	//文件句柄  
	intptr_t hFile = 0;
	//文件信息  
	struct _finddata_t fileinfo;  //很少用的文件信息读取结构
	string p;  //string类很有意思的一个赋值函数:assign()，有很多重载版本
	if ((hFile = _findfirst(p.assign(path).append("\\*").c_str(), &fileinfo)) != -1)
	{
		do
		{
			if ((fileinfo.attrib &  _A_SUBDIR))  //比较文件类型是否是文件夹
			{
				if (strcmp(fileinfo.name, ".") != 0 && strcmp(fileinfo.name, "..") != 0)
				{
					files.push_back(p.assign(path).append("/").append(fileinfo.name));
					getAllFiles(p.assign(path).append("/").append(fileinfo.name), files);
				}
			}
			else
			{
				files.push_back(p.assign(path).append("/").append(fileinfo.name));
			}
		} while (_findnext(hFile, &fileinfo) == 0);  //寻找下一个，成功返回0，否则-1
		_findclose(hFile);
	}
}

/*
初始化2个SVM。
*/
int initSVM() {
	/*
	高斯核，参数效果：
	1. gamma=0.5, C=1;	错误率49%。
	2. gamma=0.5, C=20;	64%。
	3. gamma=0.5, C=80;	64%。
	4. gamma=0.5, C=160.
	5. gamma=5,	C=20.	43%。
	6. gamma=10,C=20.	58%.
	*/
	rbfSvmPtr = SVM::create();
	rbfSvmPtr->setKernel(SVM::RBF);
	rbfSvmPtr->setType(SVM::C_SVC);	//支持向量机的类型。
	rbfSvmPtr->setGamma(5);
	rbfSvmPtr->setC(20);	//惩罚因子。
	rbfSvmPtr->setTermCriteria(TermCriteria(CV_TERMCRIT_ITER, 100, FLT_EPSILON));

	linearSvmPtr = SVM::create();
	linearSvmPtr->setKernel(SVM::LINEAR);
	linearSvmPtr->setType(SVM::C_SVC);	//支持向量机的类型。
	linearSvmPtr->setC(20);
	linearSvmPtr->setTermCriteria(TermCriteria(CV_TERMCRIT_ITER, 100, FLT_EPSILON));

	cout << "Succeed to initialize 2 SVMs." << endl;

	return 0;
}

/*
统计1个GNT文件中的汉字图像样本。
返回：
classNumber:有多少个汉字类别。
labels:这些汉字类别的编码（可能里面有前一个GNT文件中的数据）。
samples:每个汉字的样本数量。
labels和samples是降序排列。
*/
int GNTStatistics(const string &gntFilepath, map<int,int> &labelMap) {
	GNT_HEADER header;
	int totalSampleCnt = 0, imgSize, label,sampleCnt;
	map<int,int>::iterator it;
	ifstream is(gntFilepath, ios::in | ios::binary);
	if (!is)	{
		cerr << "FAIL to open input GNT file!" << endl;
		return 1;
	}
	is.read((char*)&header, GNT_HEADER_SIZE);	//读取该GNT的第1个汉字。
	while (is) {
		imgSize = header.width * header.height;
		is.read((char*)fileBuf, imgSize);
		if (!is) {
			cerr << "error: only " << is.gcount() << " could be read" << endl;
			break;
		} else {
			totalSampleCnt++;	//汉字样本总数量。
		}
		label = readChineseFromTagCode(header);
		it = labelMap.find(label);
		if (it != labelMap.end()) {
			//found.
			sampleCnt = it->second;
			sampleCnt++;
			labelMap.erase(it);
			labelMap.insert(pair<int,int>(label,sampleCnt));
		}
		else {
			labelMap.insert(pair<int, int>(label, 1));
		}

		is.read((char*)&header, GNT_HEADER_SIZE);	//继续读取GNT中的后续汉字。
	}

	is.close();

	return 0;
}
int cmp(const pair<int, int>& x, const pair<int, int>& y)
{
	return x.second > y.second;
}
/*
从全部的训练集GNT文件中，选择图像样本数量最多的汉字（降序排列的前n个）。
统计HWDB的样本情况，包括：GNT文件总数、汉字图片样本总数、不重复的汉字总数、每个汉字的图片样本数量（降序）。
返回：
classes:这些被选中汉字的GB码。
*/
int miniBatchSelect(const string &trainsetDir, const int n, int *labels) {
	vector<string> gntTrainFiles;
	map<int, int> labelMap;//key is 汉字GB码, value is 该汉字的图像样本数量.
	vector<pair<int, int> > sortVec;
	int i;
	unsigned char gb[2];
	getAllFiles(trainsetDir, gntTrainFiles);
	cout << "HWDB1.1训练集GNT文件总数：" << gntTrainFiles.size() << endl;
	for (i = 0; i != gntTrainFiles.size(); i++) {
		GNTStatistics(gntTrainFiles[i], labelMap);
		cout << "After " << gntTrainFiles[i] << ", chinese class:" << labelMap.size() << endl;;
	}
	for (map<int, int>::iterator curr = labelMap.begin(); curr != labelMap.end(); curr++) {
		sortVec.push_back(make_pair(curr->first, curr->second));
	}
	sort(sortVec.begin(), sortVec.end(), cmp);	//降序排序。
	for (i = 0; i != n; i++) {
		getTagCodeFromInt(sortVec[i].first, gb);
		printf("%c%c has %d train samples.\n", gb[0],gb[1], sortVec[i].second);
		labels[i] = sortVec[i].first;
	}

	return 0;
}

/*
根据小批指定的汉字GB码，从所有GNT中提取它们的图像，计算它们的HOG特征，进行SVM训练。
*/
int miniBatchTrain(const string &trainsetDir, const int n, const int *selectLabels) {
	vector<string> gntTrainFiles;
	vector<Mat> images;
	vector<int> labels,selectLabelVec;
	int i,j,imgSize;
	getAllFiles(trainsetDir, gntTrainFiles);
	cout << "HWDB1.1训练集GNT文件总数：" << gntTrainFiles.size() << endl;
	for (i = 0; i != n; i++)	selectLabelVec.push_back(selectLabels[i]);
	for (i = 0; i != gntTrainFiles.size(); i++) {
		readAGnt(gntTrainFiles[i], images, labels, selectLabelVec);
		imgSize = images.size();
		Mat featureMat(imgSize, FEATURE_DIMENSION, CV_32FC1);	//1个image1行特征。
		computeHog(images, featureMat);
		//CV_Assert(featureMat.rows == subTrainSize && featureMat.cols == FEATURE_VECTOR_SIZE);
		trainFeatureMat.push_back(featureMat);	//把featureMat追加到trainFeatureMat尾部。
		featureMat.release();

		Mat labelMat(imgSize, 1, CV_32SC1);
		for (j = 0; j != imgSize; j++) {
			labelMat.ptr<int>(j)[0] = static_cast<int>(labels[j]);	//保存分类结果。
		}
		trainLabelMat.push_back(labelMat);	//把labelMat追加到trainLabelMat尾部。
		labelMat.release();

		images.clear();
		labels.clear();

		cout << "trainFeatureMat rows:"<<trainFeatureMat.rows<<", cols:"<< trainFeatureMat.cols<<endl;
	}
	
	cout << "Begin to init SVM and train, please wait......"<<endl;
	double timeStart = static_cast<double>(cv::getTickCount());
	initSVM();
	linearSvmPtr->train(trainFeatureMat, ROW_SAMPLE, trainLabelMat);
	linearSvmPtr->save("output/SVM_minibatch.yml");
	double duration = (cv::getTickCount() - timeStart) / cv::getTickFrequency();
	cout << "It takes "<<duration<<" seconds to train SVM(linear)."<<endl;

	return 0;
}

/*
从所有测试集中选择指定的汉字进行预测。
*/
int miniBatchPredict(const string &testsetDir, const int n, const int *selectLabels) {
	vector<string> gntTestFiles;
	vector<Mat> images;
	vector<int> labels, selectLabelVec;
	int i, j, imgSize,errorCnt=0,predictAmt=0;
	float response;
	unsigned char realCode[2], predictCode[2];
	getAllFiles(testsetDir, gntTestFiles);
	cout << "HWDB1.1测试集GNT文件总数：" << gntTestFiles.size() << endl;
	for (i = 0; i != n; i++)	selectLabelVec.push_back(selectLabels[i]);
	for (i = 0; i != gntTestFiles.size(); i++) {
		readAGnt(gntTestFiles[i], images, labels, selectLabelVec);
		imgSize = images.size();
		predictAmt += imgSize;
		for (j = 0; j != imgSize; j++) {
			Mat featureMat(1, FEATURE_DIMENSION, CV_32FC1);	//1个image1行特征。
			computeAHog(images[j], featureMat);
			response = linearSvmPtr->predict(featureMat);
			if (labels[j] != static_cast<int>(response)) {
				//预测错误。
				errorCnt++;
				getTagCodeFromInt(labels[j], realCode);
				getTagCodeFromInt(static_cast<int>(response), predictCode);
				printf("%c%c is WRONGLY predicted to %c%c.\n", realCode[0],realCode[1], predictCode[0], predictCode[1]);
			} else {
			}
		}
	}
	float testErrorRate = float(errorCnt) / predictAmt * 100.0f;
	cout << "HWDB1.1_minibatch SVM(linear) + HOG predictAmt:"<<predictAmt<<", error rate :" << testErrorRate << endl;

	return 0;
}

/*
取n个图像最多的汉字进行训练。
*/
int miniBatch() {
	const int miniSelected = 20;	//从全部训练集中选择10个汉字。
	int miniLabels[miniSelected];	//保存汉字编码（降序）。
	miniBatchSelect(TRAINSET_DIR,	miniSelected, miniLabels);
	miniBatchTrain(TRAINSET_DIR,	miniSelected, miniLabels);
	miniBatchPredict(TESTSET_DIR,	miniSelected, miniLabels);

	return 0;
}

int _tmain(int argc, _TCHAR* argv[])
{
	cout<<"hello, HWDB with (OpenCV 3.4.1 x64)!" << endl;
	unsigned short featureDimension = computeHogFeatureDimension(winSize, winSize, blockSize, blockStride, cellSize, nbins);
	cout << "feature dimension:" << featureDimension << endl;
	CV_Assert(FEATURE_DIMENSION==featureDimension);

	miniBatch();

	/*
	vector<string> gntTrainFiles,gntTestFiles;
	getAllFiles(GNT_TRAINSET_PATH, gntTrainFiles);
	getAllFiles(GNT_TESTSET_PATH, gntTestFiles);
	const int trainGntSize = gntTrainFiles.size();
	const int testGntSize = gntTestFiles.size();

	double timeStart, duration,sumGenImg=0,sumHogDur=0;
	vector<Mat> images;
	vector<int> labels;
	vector<int> gbcodes;	//存放不重复出现的汉字代码。
	vector<int>::iterator it;
	int i, j, subTrainSize, totalTrainSize,totalCharCnt=0;
	for (i = 0; i != trainGntSize; i++) {
		timeStart = static_cast<double>(cv::getTickCount());
		cout << i <<"/"<< trainGntSize << " begin to read samples from file " << gntTrainFiles[i] << endl;
		readAGnt(gntTrainFiles[i], images, labels);
		subTrainSize = images.size();	//当前GNT包含的汉字数量。
		totalCharCnt += subTrainSize;
		duration = (cv::getTickCount() - timeStart) / cv::getTickFrequency();
		sumGenImg += duration;

		timeStart = static_cast<double>(cv::getTickCount());
		Mat featureMat(subTrainSize, FEATURE_DIMENSION, CV_32FC1);
		computeHog(images, featureMat);
		//CV_Assert(featureMat.rows == subTrainSize && featureMat.cols == FEATURE_VECTOR_SIZE);
		totalTrainSize = trainFeatureMat.rows;
		trainFeatureMat.push_back(featureMat);
		//CV_Assert(trainFeatureMat.rows == subTrainSize + totalTrainSize);

		Mat labelMat(subTrainSize, 1, CV_32SC1);
		for (j = 0; j != subTrainSize; j++) {
			labelMat.ptr<int>(j)[0] = static_cast<int>(labels[j]);	//保存分类结果。
			it = find(gbcodes.begin(), gbcodes.end(), labels[j]);
			if (it == gbcodes.end())	gbcodes.push_back(labels[j]);
		}
		trainLabelMat.push_back(labelMat);
		duration = (cv::getTickCount() - timeStart) / cv::getTickFrequency();
		sumHogDur += duration;

		featureMat.release();
		labelMat.release();
		labels.clear();
		images.clear();
		
		cout <<"It takes "<<sumGenImg<<" seconds to generate image Mat, "<<sumHogDur<<" seconds to compute hog feature."<<endl;
		cout << "Trainset feature mat rows:" << trainFeatureMat.rows <<", cols:"<< trainFeatureMat.cols<<endl;
		cout << "Trainset label mat rows:" << trainLabelMat.rows << ", cols:" << trainLabelMat.cols << endl;
	}
	cout <<"There are total "<<totalCharCnt<<" chinese are featured, "<<gbcodes.size()<<" are non-duplicated."<< endl;

	initSVM();
	cout <<"Begin to SVM(linear) longterm train, please wait......" <<endl;
	timeStart = static_cast<double>(cv::getTickCount());
	linearSvmPtr->train(trainFeatureMat, ROW_SAMPLE, trainLabelMat);
	trainFeatureMat.release();
	trainLabelMat.release();
	duration = (cv::getTickCount() - timeStart) / cv::getTickFrequency();
	cout <<"It takes "<<duration<<"seconds to train SVM(linear)." <<endl;

	stringstream ss;
	string filename;
	int iTest,iFact,subTestSize,errorCnt=0,totalCnt=0;
	float response;
	unsigned char realTagCode[2],predictTagCode[2];
	for (i = 0; i != testGntSize; i++) {
		images.clear();
		labels.clear();
		readAGnt(gntTestFiles[i], images, labels);
		subTestSize = images.size();	//当前GNT包含的汉字数量。
		cout << i << "/" << testGntSize << " gnt file, "<<"begin to predict "<<subTestSize<<" chinese character."<<endl;
		for (j = 0; j != subTestSize; j++) {
			iFact = labels[j];	//该汉字实际的GB码。
			Mat featureMat(1, FEATURE_DIMENSION, CV_32FC1);
			computeAHog(images[j], featureMat);
			cout <<"featureMat rows:"<<featureMat.rows<<", cols:"<<featureMat.cols <<endl;
			response = linearSvmPtr->predict(featureMat);	//预测sampleMat的分类标签label。
			iTest = static_cast<int>(response);	//该汉字预测的GB码。
			totalCnt++;	//预测的汉字总数。
			getTagCodeFromInt(iFact, realTagCode);
			getTagCodeFromInt(iTest, predictTagCode);
			if (iTest != iFact) {
				errorCnt++;
				printf("%c%c is wrongly predict to %c%c.\n", realTagCode[0],realTagCode[1], predictTagCode[0],predictTagCode[1]);
			}
			else {
				printf("%c%c is correctly predict.\n", predictTagCode[0], predictTagCode[1]);
			}
			featureMat.release();
		}
		cout <<i<<"/"<<testGntSize<<" gnt file, predict total "<<totalCnt<<"chinese char, error:"<<errorCnt <<endl;
	}
	//以上是统计分类错误率。
	float testErrorRate = float(errorCnt) / totalCnt * 100.0f;
	cout <<"SVM(linear) + HOG error rate :"<<testErrorRate <<endl;
	*/

	return 0;
}