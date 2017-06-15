#include "checkpoint/CheckPoint.h"
#include "sim/run_stride.h"

#include <exception>
#include <iostream>
#include <memory>
#include <tclap/CmdLine.h>

using namespace std;
using namespace stride;
using namespace TCLAP;

std::unique_ptr<checkpoint::CheckPoint> cp;

void storeCP(const std::string& config, const std::string& disease, const std::string& matrix)
{
	if(!config.empty()){
		std::cout<<"Storing config"<<std::endl;
		cp->OpenFile();
		cp->StoreConfig(config);
		cp->CloseFile();
	}
	if(!disease.empty()){
		std::cout<<"Storing disease"<<std::endl;
		cp->OpenFile();
		cp->StoreDisease(disease);
		cp->CloseFile();
	}
	if(!matrix.empty()){
		std::cout<<"Storing contact matrix"<<std::endl;
		cp->OpenFile();
		cp->StoreMatrix(matrix);
		cp->CloseFile();
	}
	std::cout<<"Done storing"<<std::endl;
}

void loadCP(const std::string& config, const std::string& disease, const std::string& matrix)
{
	if(!config.empty()){
		std::cout<<"Loading config"<<std::endl;
		cp->OpenFile();
		cp->LoadConfig(config);
		cp->CloseFile();
	}
	if(!disease.empty()){
		std::cout<<"Loading disease"<<std::endl;
		cp->OpenFile();
		cp->LoadDisease(disease);
		cp->CloseFile();
	}
	if(!matrix.empty()){
		std::cout<<"Loading contact matrix"<<std::endl;
		cp->OpenFile();
		cp->LoadMatrix(matrix);
		cp->CloseFile();
	}
	std::cout<<"Done loading"<<std::endl;
}

/// Main program of the stridecp program.
int main(int argc, char** argv)
{
	int exit_status = EXIT_SUCCESS;
	try {
		// -----------------------------------------------------------------------------------------
		// Parse command line.
		// -----------------------------------------------------------------------------------------
		CmdLine cmd("strideCP", ' ', "1.0", false);

		SwitchArg store("s", "store", "The given files will be stored in the checkpoint", cmd, false);

		ValueArg<string> h5File("f", "h5file", "HDF5 file to write to", false, "", "CHECKPOINT FILE", cmd);

		ValueArg<string> configfile(
		    "c", "config", "The config file to load into or load from", false, "", "JSON FILE", cmd);

		ValueArg<string> diseasefile(
		    "d", "disease", "The disease file to load into or load from", false, "", "XML FILE", cmd);

		ValueArg<string> matrixfile(
		    "m", "contact-matrix", "The contact matrix to load into or load from", false, "", "XML FILE", cmd);

		UnlabeledValueArg<string> checkpointfile(
		    "checkpoint", "the file containing the checkpoint", true, "", "H5 FILE", cmd);

		cmd.parse(argc, argv);

		// -----------------------------------------------------------------------------------------
		// Print output to command line.
		// -----------------------------------------------------------------------------------------
		print_execution_environment();

		// -----------------------------------------------------------------------------------------
		// Check execution environment.
		// -----------------------------------------------------------------------------------------
		verify_execution_environment();
		cp = std::make_unique<checkpoint::CheckPoint>(checkpointfile.getValue());
		if (store.getValue()) {
			storeCP(configfile.getValue(), diseasefile.getValue(), matrixfile.getValue());
		} else {
			loadCP(configfile.getValue(), diseasefile.getValue(), matrixfile.getValue());
		}

	} catch (exception& e) {

		exit_status = EXIT_FAILURE;
		cerr << "\nEXCEPION THROWN: " << e.what() << endl;
	} catch (...) {
		exit_status = EXIT_FAILURE;
		cerr << "\nEXCEPION THROWN: "
		     << "Unknown exception." << endl;
	}
	return exit_status;
}