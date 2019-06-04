//** Object Detection streaming version – with GPU


#include <vector>
#include <memory>
#include <string>

#include <opencv2/opencv.hpp>
#include <inference_engine.hpp>
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/imgproc/imgproc.hpp>

const char *labels[20] = {"plane", "bicycle", "bird", "boat", "bottle", "bus", "car", "cat", "chair", "cow", "table",
			"dog", "horse", "motorcycle", "person", "plant", "sheep", "sofa", "train", "monitor"};

using namespace std;
using namespace cv;
using namespace InferenceEngine;

int main(int argc, char *argv[]) {

	// ---------------------Load A Plugin for Inference Engine-----------------------------------------

	InferenceEngine::PluginDispatcher dispatcher({""});
	InferencePlugin plugin(dispatcher.getSuitablePlugin(TargetDevice::eGPU));


	// --------------------Load IR Generated by ModelOptimizer (.xml and .bin files)------------------------

	CNNNetReader network_reader;
	
	network_reader.ReadNetwork("/home/intel/my_model/ssd300.xml");
	network_reader.ReadWeights("/home/intel/my_model/ssd300.bin");
	network_reader.getNetwork().setBatchSize(1);

	CNNNetwork network = network_reader.getNetwork();

	// -----------------------------Prepare input blobs-----------------------------------------------------

	auto input_info = network.getInputsInfo().begin()->second;
	auto input_name = network.getInputsInfo().begin()->first;

	input_info->setPrecision(Precision::U8);

	// ---------------------------Prepare output blobs------------------------------------------------------

	auto output_info = network.getOutputsInfo().begin()->second;
	auto output_name = network.getOutputsInfo().begin()->first;
	
	output_info->setPrecision(Precision::FP32);

	// -------------------------Loading model to the plugin and then infer----------------------------------

	auto executable_network = plugin.LoadNetwork(network, {});
	auto infer_request = executable_network.CreateInferRequest();

	auto input = infer_request.GetBlob(input_name);
	auto input_data = input->buffer().as<PrecisionTrait<Precision::U8>::value_type*>();

	VideoCapture cap("/dev/video0");	//<-- usb camera or can be any video stream

	if(!cap.isOpened())
	{
		cout << "can't open input device" << endl;
		return 1;
	}

	Mat ori_image, infer_image;

	while(1)
	{
		// read one frame from input stream
		cap.read(ori_image);
		
		resize(ori_image, infer_image, Size(input_info->getDims()[0], input_info->getDims()[1]));

		//namedWindow("infer", WINDOW_NORMAL);
		//resizeWindow("infer", 600,600);
		//imshow("infer", infer_image);
		//waitKey(0);

		size_t channels_number = input->dims()[2];
		size_t image_size = input->dims()[1] * input->dims()[0];

		for(size_t pid = 0; pid < image_size; ++pid) {
			for(size_t ch = 0; ch < channels_number; ++ch)	{
				input_data[ch*image_size+pid] = infer_image.at<Vec3b>(pid)[ch];
			}
		}

		/* Running the request synchronously */
		infer_request.Infer();

		// ---------------------------Postprocess output blobs--------------------------------------------------

		auto output = infer_request.GetBlob(output_name);

		// +++++++++++++ check proposal count and objectsize of each proposal +++++++++++
		const int maxProposalCount = output->dims()[1];
		const int objectSize = output->dims()[0];

		const Blob::Ptr output_blob = output;
		const float* detection = static_cast<PrecisionTrait<Precision::FP32>::value_type*>(output_blob->buffer());

		/* Each detection has image_id that denotes processed image */
		for (int curProposal = 0; curProposal < maxProposalCount; curProposal++) {
			float image_id = detection[curProposal * objectSize + 0];
			float label_index = detection[curProposal * objectSize + 1];
			float confidence = detection[curProposal * objectSize + 2];
			/* CPU and GPU plugins have difference in DetectionOutput layer, so we need both checks */
			if (image_id < 0 || confidence == 0) {
				continue;
			}

			float xmin = detection[curProposal * objectSize + 3] * ori_image.size().width;
			float ymin = detection[curProposal * objectSize + 4] * ori_image.size().height;
			float xmax = detection[curProposal * objectSize + 5] * ori_image.size().width;
			float ymax = detection[curProposal * objectSize + 6] * ori_image.size().height;

			cout << "[" << curProposal << "," << label_index << "] element, prob = " << confidence << "    (" << xmin << "," << ymin << ")-(" << xmax << "," << ymax << ")" << " batch id : " << image_id;

			if (confidence > 0.5) {
			/** Drawing only objects with >50% probability **/
				string stag;
				ostringstream ostr;                        
				ostr << labels[(int)label_index - 1] << ", " << fixed << setw(4) << setprecision(2) << confidence;
				string header = ostr.str();
		
				rectangle(ori_image, Point((int)xmin, (int)ymin), Point((int)xmax, (int)ymax), Scalar(0, 230, 0), 2, 4);
				rectangle(ori_image, Point((int)xmin-1, (int)ymin-16), Point((int)xmax+1, (int)ymin), Scalar(0, 230, 0), CV_FILLED, LINE_8, 0);
				putText(ori_image, header, Point((int)xmin+4, (int)ymin), FONT_HERSHEY_TRIPLEX, .5, Scalar(70,70,70));
				putText(ori_image, header, Point((int)xmin+3, (int)ymin-1), FONT_HERSHEY_TRIPLEX, .5, Scalar(0,0,0));
				cout << " WILL BE PRINTED!";
			}  		
			cout << endl;
		}

		imshow("result", ori_image);
		
		if (waitKey(1) >= 13)	// enterkey
			break;
	}

	return 0;
}
