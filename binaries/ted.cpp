/**
 * edit distance main file. Initializes all objects, views, and visualizers.
 */

#include <iostream>
#include <fstream>
#include <imageprocessing/io/ImageStackDirectoryReader.h>
#include <imageprocessing/io/ImageStackDirectoryWriter.h>
#include <pipeline/Process.h>
#include <pipeline/Value.h>
#include <evaluation/ErrorReport.h>
#include <evaluation/ExtractGroundTruthLabels.h>
#include <evaluation/TolerantEditDistanceErrorsWriter.h>
#include <util/ProgramOptions.h>
#include <util/Logger.h>
#include <boost/filesystem.hpp>
#ifdef HAVE_HDF5
#include <vigra/hdf5impex.hxx>
#endif

using namespace logger;

util::ProgramOption optionGroundTruth(
		util::_long_name        = "groundTruth",
		util::_description_text = "The ground truth image stack.",
		util::_default_value    = "groundtruth");

util::ProgramOption optionExtractGroundTruthLabels(
		util::_long_name        = "extractGroundTruthLabels",
		util::_description_text = "Indicate that the ground truth consists of a foreground/background labeling "
		                          "(dark/bright) and each 4-connected component of foreground represents one region.");

util::ProgramOption optionExportGroundtruth(
		util::_long_name        = "exportGroundTruth",
		util::_description_text = "If extractGroundTruthLabels is set, use this option to export the labeled groundtruth.");

util::ProgramOption optionReconstruction(
		util::_long_name        = "reconstruction",
		util::_description_text = "The reconstruction image stack.",
		util::_default_value    = "reconstruction");

util::ProgramOption optionPlotFile(
		util::_long_name        = "plotFile",
		util::_description_text = "Append a tab-separated single-line error report to the given file.");

util::ProgramOption optionPlotFileHeader(
		util::_long_name        = "plotFileHeader",
		util::_description_text = "Instead of computing the errors, print a single-line header in the plot file.");

util::ProgramOption optionTedErrorFiles(
		util::_long_name        = "tedErrorFiles",
		util::_description_text = "Folder where to create files splits.dat and merges.dat (with background label als fps.dat and fns.dat)"
		                          "which report which label got split/merged into which.");

util::ProgramOption optionReportVoi(
		util::_module           = "evaluation",
		util::_long_name        = "reportVoi",
		util::_description_text = "Compute variation of information for the error report.");

util::ProgramOption optionReportRand(
		util::_module           = "evaluation",
		util::_long_name        = "reportRand",
		util::_description_text = "Compute the RAND index for the error report.");

util::ProgramOption optionReportDetectionOverlap(
		util::_module           = "evaluation",
		util::_long_name        = "reportDetectionOverlap",
		util::_description_text = "Compute the detection overlap for the error report.",
		util::_default_value    = true);

util::ProgramOption optionReportTed(
		util::_module           = "evaluation",
		util::_long_name        = "reportTed",
		util::_description_text = "Compute the tolerant edit distance for the error report.",
		util::_default_value    = true);

util::ProgramOption optionIgnoreBackground(
		util::_module           = "evaluation",
		util::_long_name        = "ignoreBackground",
		util::_description_text = "For the computation of VOI and RAND, do not consider background pixels in the ground truth.");

util::ProgramOption optionGrowSlices(
		util::_module           = "evaluation",
		util::_long_name        = "growSlices",
		util::_description_text = "For the computation of VOI and RAND, grow the reconstruction slices until no background label is present anymore.");

std::string buildCorrectedPath(std::string root, std::string reconstructionPath) {
	boost::filesystem::path reconstruction = reconstructionPath;
	std::string folderName = "corrected_" + reconstruction.stem().string();
	boost::filesystem::path p = root;
	p /= folderName ;
	return p.string();

}

std::string buildReportPath(std::string root, std::string reconstructionPath, std::string type) {
	boost::filesystem::path reconstruction = reconstructionPath;
	std::string fileName = reconstruction.stem().string()  + "." + type + ".data";
	boost::filesystem::path p = root;
	p /= fileName;
	return p.string();
}


void readImageStackFromOption(ImageStack& stack, std::string option) {

	// hdf file given?
	size_t sepPos = option.find_first_of(":");
	if (sepPos != std::string::npos) {

#ifdef HAVE_HDF5
		std::string hdfFileName = option.substr(0, sepPos);
		std::string dataset     = option.substr(sepPos + 1);

		vigra::HDF5File file(hdfFileName, vigra::HDF5File::OpenMode::ReadOnly);

		vigra::MultiArray<3, float> volume;
		file.readAndResize(dataset, volume);

		stack.clear();
		for (int z = 0; z < volume.size(2); z++) {
			boost::shared_ptr<Image> image = boost::make_shared<Image>(volume.size(0), volume.size(1));
			vigra::MultiArrayView<2, float> imageView = *image;
			imageView = volume.bind<2>(z);
			stack.add(image);
		}

		vigra::MultiArray<1, float> p(3);

		if (file.existsAttribute(dataset, "resolution")) {

			// resolution
			file.readAttribute(
					dataset,
					"resolution",
					p);
			stack.setResolution(p[0], p[1], p[2]);
		}
#else
		UTIL_THROW_EXCEPTION(
				UsageError,
				"This build does not support reading form HDF5 files. Set CMake variable BUILD_WITH_HDF5 and recompile.");
#endif

	// read stack from directory of images
	} else {

		pipeline::Process<ImageStackDirectoryReader> stackReader(option);

		pipeline::Value<ImageStack> output = stackReader->getOutput();
		stack = *output;
	}
}

int main(int optionc, char** optionv) {

	try {

		/********
		 * INIT *
		 ********/

		// init command line parser
		util::ProgramOptions::init(optionc, optionv);

		// init logger
		LogManager::init();
		Logger::showChannelPrefix(false);

		/*********
		 * SETUP *
		 *********/

		// setup error report

		ErrorReport::Parameters parameters;
		parameters.headerOnly = optionPlotFileHeader.as<bool>();
		parameters.reportTed = optionReportTed.as<bool>();
		parameters.reportRand = optionReportRand.as<bool>();
		parameters.reportVoi = optionReportVoi.as<bool>();
		parameters.reportDetectionOverlap = optionReportDetectionOverlap.as<bool>();
		parameters.ignoreBackground = optionIgnoreBackground.as<bool>();
		parameters.growSlices = optionGrowSlices.as<bool>();

		pipeline::Process<ErrorReport> report(parameters);

		if (optionPlotFileHeader) {

			std::ofstream f(optionPlotFile.as<std::string>(), std::ofstream::app);
			pipeline::Value<std::string> reportText = report->getOutput("error report header");

			f << *reportText << std::endl;
			return 0;
		}

		// setup file readers and writers

		pipeline::Value<ImageStack> groundTruth;
		pipeline::Value<ImageStack> reconstruction;

		readImageStackFromOption(*groundTruth, optionGroundTruth);
		readImageStackFromOption(*reconstruction, optionReconstruction);

		report->setInput("reconstruction", reconstruction);

		if (optionExtractGroundTruthLabels) {

			LOG_DEBUG(out) << "[main] extracting ground truth labels from connected components" << std::endl;

			pipeline::Process<ExtractGroundTruthLabels> extractLabels;
			extractLabels->setInput(groundTruth);
			report->setInput("ground truth", extractLabels->getOutput());

			if (optionExportGroundtruth) {

				pipeline::Process<ImageStackDirectoryWriter> writer("groundtruth");
				writer->setInput(extractLabels->getOutput());
				writer->write();
			}

		} else {

			report->setInput("ground truth", groundTruth);
		}

		try {

			// save corrected reconstruction
			pipeline::Process<ImageStackDirectoryWriter> 
				correctedWriter(buildCorrectedPath(optionTedErrorFiles, optionReconstruction));
			correctedWriter->setInput(report->getOutput("ted corrected reconstruction"));
			correctedWriter->write();

		} catch (pipeline::ProcessNode::NoSuchOutput& e) {

			// well, we tried...
		}

		// write error report
		pipeline::Value<std::string> reportText = report->getOutput("human readable error report");
		LOG_USER(out) << *reportText << std::endl;

		if (optionTedErrorFiles) {

			// list of split, merge, fp, and fn errors
			pipeline::Value<TolerantEditDistanceErrors> errors = report->getOutput("ted errors");

			std::ofstream splitFile(buildReportPath(optionTedErrorFiles, optionReconstruction, "splits"));
			foreach (float gtLabel, errors->getSplitLabels()) {
				splitFile << gtLabel << "\t";
				foreach (float recLabel, errors->getSplits(gtLabel))
					splitFile << recLabel << "\t";
				splitFile << std::endl;
			}
			std::ofstream mergeFile(buildReportPath(optionTedErrorFiles, optionReconstruction, "merges"));
			foreach (float recLabel, errors->getMergeLabels()) {
				mergeFile << recLabel << "\t";
				foreach (float gtLabel, errors->getMerges(recLabel))
					mergeFile << gtLabel << "\t";
				mergeFile << std::endl;
			}

			if (errors->hasBackgroundLabel()) {
				std::ofstream fpFile(buildReportPath(optionTedErrorFiles, optionReconstruction, "fps"));
				foreach (float recLabel, errors->getFalsePositives())
					fpFile << recLabel << std::endl;
				std::ofstream fnFile(buildReportPath(optionTedErrorFiles, optionReconstruction, "fns"));
				foreach (float gtLabel, errors->getFalseNegatives())
					fnFile << gtLabel << std::endl;
			}
		}

		if (optionPlotFile) {

			std::ofstream f(optionPlotFile.as<std::string>(), std::ofstream::app);
			pipeline::Value<std::string> reportText = report->getOutput("error report");

			f << *reportText << std::endl;

		}

	} catch (Exception& e) {

		handleException(e, std::cerr);
	}
}


