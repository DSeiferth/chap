#include <iostream>
#include <string>
#include <vector>

//#include <gromacs/trajectoryanalysis.h>
#include <gromacs/commandline/cmdlineinit.h>
#include <gromacs/commandline/cmdlineprogramcontext.h>
#include <gromacs/commandline.h>

#include "config/config.hpp"
#include "config/back_matter.hpp"
#include "config/front_matter.hpp"
#include "config/version.hpp"
#include "trajectory-analysis/trajectory-analysis.hpp"

using namespace gmx;


int main(int argc, char **argv)
{
    // print front matter:
    FrontMatter::print();

    // hack to suppress Gromacs output:
    std::vector<char*> modArgv(argv, argv + argc);
    char quiet[7] = "-quiet";
    modArgv.push_back(quiet);
    modArgv.push_back(nullptr);
    argv = modArgv.data();
    argc++;

    // run trajectory analysis:
	int status =  gmx::TrajectoryAnalysisCommandLineRunner::runAsMain<trajectoryAnalysis>(argc, argv);

    // print back matter:
    BackMatter::print();

    // return status:
    return status;
}











