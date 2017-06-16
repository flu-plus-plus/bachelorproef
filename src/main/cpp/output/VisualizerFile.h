#ifndef VIS_FILE_H_INCLUDED
#define VIS_FILE_H_INCLUDED
/**
 * @file
 * Header for the VisualizerFile class.
 */

#include <fstream>
#include <string>
#include <vector>
#include "VisualizerData.h"
#include "core/Atlas.h"

namespace stride {
namespace output {

/**
 * Produces a file with the necessary data for visualization.
 */
class VisualizerFile
{
public:
	/// Constructor: initialize.
	VisualizerFile(const std::string& file = "stride_vis");

	/// Destructor: close the file stream.
	~VisualizerFile();

	/// Print the given visualisation data.
	void Print(VisualizerData& visualizer_data);

private:
	/// Generate file name and open the file stream.
	void Initialize(const std::string& file);

private:
	std::ofstream m_fstream;
};

} // end_of_namespace
} // end_of_namespace

#endif // end of include guard
