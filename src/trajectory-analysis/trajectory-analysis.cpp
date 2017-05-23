#include <algorithm>	// for std::max_element()
#include <cmath>		// for std::sqrt()
#include <fstream>
#include <iomanip>
#include <string>
#include <cstring>
#include <ctime>

#include <gromacs/topology/atomprop.h>
#include <gromacs/random/threefry.h>
#include <gromacs/random/uniformrealdistribution.h>
#include <gromacs/fileio/confio.h>

#include "trajectory-analysis/trajectory-analysis.hpp"

#include "geometry/spline_curve_1D.hpp"
#include "geometry/spline_curve_3D.hpp"
#include "geometry/cubic_spline_interp_1D.hpp"
#include "geometry/cubic_spline_interp_3D.hpp"

#include "io/molecular_path_obj_exporter.hpp"

#include "trajectory-analysis/simulated_annealing_module.hpp"
#include "trajectory-analysis/path_finding_module.hpp"
#include "trajectory-analysis/analysis_data_long_format_plot_module.hpp"
#include "trajectory-analysis/analysis_data_pdb_plot_module.hpp"

#include "path-finding/inplane_optimised_probe_path_finder.hpp"
#include "path-finding/optimised_direction_probe_path_finder.hpp"
#include "path-finding/naive_cylindrical_path_finder.hpp"

using namespace gmx;



/*
 * Constructor for the trajectoryAnalysis class.
 */
trajectoryAnalysis::trajectoryAnalysis()
    : cutoff_(0.0)
    , pfMethod_("inplane-optim")
    , pfProbeStepLength_(0.1)
    , pfProbeRadius_(0.0)
    , pfMaxFreeDist_(1.0)
    , pfMaxProbeSteps_(1e3)
    , pfInitProbePos_(3)
    , pfChanDirVec_(3)
    , saRandomSeed_(15011991)
    , saMaxCoolingIter_(1e3)
    , saNumCostSamples_(50)
    , saConvRelTol_(1e-10)
    , saInitTemp_(10.0)
    , saCoolingFactor_(0.99)
    , saStepLengthFactor_(0.01)
    , saUseAdaptiveCandGen_(false)
{
    //
    registerAnalysisDataset(&data_, "somedata");
    data_.setMultipoint(true);              // mutliple support points 
    
       // register dataset:
    registerAnalysisDataset(&dataResMapping_, "resMapping");
 



    // default initial probe position and chanell direction:
    pfInitProbePos_ = {0.0, 0.0, 0.0};
    pfChanDirVec_ = {0.0, 0.0, 1.0};


}



/*
 *
 */
void
trajectoryAnalysis::initOptions(IOptionsContainer          *options,
                                TrajectoryAnalysisSettings *settings)
{
	// set help text:
	static const char *const desc[] = {
		"This is a first prototype for the CHAP tool.",
		"There is NO HELP, you are on your own!"
	};
    settings -> setHelpText(desc);

    // hardcoded defaults for multivalue options:
    std::vector<real> chanDirVec_ = {0.0, 0.0, 1.0};


	// require the user to provide a topology file input:
    settings -> setFlag(TrajectoryAnalysisSettings::efRequireTop);

	// get (required) selection option for the reference group: 
	options -> addOption(SelectionOption("reference")
	                     .store(&refsel_).required()
		                 .description("Reference group that defines the channel (normally 'Protein'): "));

	// get (required) selection options for the small particle groups:
	options -> addOption(SelectionOption("select")
                         .storeVector(&sel_).required()
	                     .description("Group of small particles to calculate density of (normally 'Water'):"));

   	// get (optional) selection options for the initial probe position selection:
	options -> addOption(SelectionOption("ippsel")
                         .store(&ippsel_)
                         .storeIsSet(&ippselIsSet_)
	                     .description("Reference group from which to determine the initial probe position for the pore finding algorithm. If unspecified, this defaults to the overall pore forming group. Will be overridden if init-probe-pos is set explicitly."));

    
    // get (optional) selection option for the neighbourhood search cutoff:
    options -> addOption(RealOption("margin")
	                     .store(&poreMappingMargin_)
                         .defaultValue(1.0)
                         .description("Margin for residue mapping."));
 

    // get (optional) selection option for the neighbourhood search cutoff:
    options -> addOption(DoubleOption("cutoff")
	                     .store(&cutoff_)
                         .description("Cutoff for distance calculation (0 = no cutoff)"));


    // output options:
    options -> addOption(StringOption("ppfn")
                         .store(&poreParticleFileName_)
                         .defaultValue("pore_particles.dat")
                         .description("Name of file containing pore particle positions over time."));
    options -> addOption(StringOption("spfn")
                         .store(&smallParticleFileName_)
                         .defaultValue("small_particles.dat")
                         .description("Name of file containing small particle positions (i.e. water particle positions) over time."));
    options -> addOption(StringOption("o")
                         .store(&poreProfileFileName_)
                         .defaultValue("pore_profile.dat")
                         .description("Name of file containing pore radius, small particle density, and small particle energy as function of the permeation coordinate."));
    options -> addOption(IntegerOption("num-out-pts")
                         .store(&nOutPoints_)
                         .defaultValue(1000)
                         .description("Number of sample points of pore centre line that are written to output."));



    // get parameters of path-finding agorithm:
    options -> addOption(StringOption("pf-method")
                         .store(&pfMethod_)
                         .defaultValue("inplane-optim")
                         .description("Path finding method. Only inplane-optim is implemented so far."));
    options -> addOption(RealOption("probe-step")
                         .store(&pfProbeStepLength_)
                         .defaultValue(0.025)
                         .description("Step length for probe movement. Defaults to 0.025 nm."));
    options -> addOption(RealOption("probe-radius")
                         .store(&pfProbeRadius_)
                         .defaultValue(0.0)
                         .description("Radius of probe. Defaults to 0.0, buggy for other values!"));
    options -> addOption(RealOption("max-free-dist")
                         .store(&pfMaxFreeDist_)
                         .defaultValue(1.0)
                         .description("Maximum radius of pore. Defaults to 1.0, buggy for larger values."));
    options -> addOption(IntegerOption("max-probe-steps")
                         .store(&pfMaxProbeSteps_)
                         .description("Maximum number of steps the probe is moved in either direction."));
    options -> addOption(RealOption("init-probe-pos")
                         .storeVector(&pfInitProbePos_)
                         .storeIsSet(&pfInitProbePosIsSet_)
                         .valueCount(3)
                         .description("Initial position of probe in probe-based pore finding algorithms. If this is set explicitly, it will overwrite the COM-based initial position set with the ippselflag."));
    options -> addOption(RealOption("chan-dir-vec")
                         .storeVector(&pfChanDirVec_)
                         .storeIsSet(&pfChanDirVecIsSet_)
                         .valueCount(3)
                         .description("Channel direction vector; will be normalised to unit vector internally. Defaults to (0, 0, 1)."));
    options -> addOption(IntegerOption("sa-random-seed")
                         .store(&saRandomSeed_)
                         .required()
                         .description("Seed for RNG used in simulated annealing."));
    options -> addOption(IntegerOption("sa-max-cool")
                          .store(&saMaxCoolingIter_)
                          .defaultValue(1000)
                          .description("Maximum number of cooling iterations in one simulated annealing run. Defaults to 1000."));
    options -> addOption(IntegerOption("sa-cost-samples")
                         .store(&saNumCostSamples_)
                         .defaultValue(10)
                         .description("NOT IMPLEMENTED! Number of cost samples considered for convergence tolerance. Defaults to 10."));
    options -> addOption(RealOption("sa-conv-tol")
                         .store(&saConvRelTol_)
                         .defaultValue(1e-3)
                         .description("Relative tolerance for simulated annealing."));
    options -> addOption(RealOption("sa-init-temp")
                         .store(&saInitTemp_)
                         .defaultValue(0.1)
                         .description("Initital temperature for simulated annealing. Defaults to 0.1."));
    options -> addOption(RealOption("sa-cooling-fac")
                         .store(&saCoolingFactor_)
                         .defaultValue(0.98)
                         .description("Cooling factor using in simulated annealing. Defaults to 0.98."));
    options -> addOption(RealOption("sa-step")
                         .store(&saStepLengthFactor_)
                         .defaultValue(0.001)
                         .description("Step length factor used in candidate generation. Defaults to 0.001.")) ;
    options -> addOption(BooleanOption("debug-output")
                         .store(&debug_output_)
                         .description("When this flag is set, the program will write additional information.")) ;
}




/*
 * 
 */
void
trajectoryAnalysis::initAnalysis(const TrajectoryAnalysisSettings &settings,
                                 const TopologyInformation &top)
{
	// set cutoff distance for grid search as specified in user input:
	nb_.setCutoff(cutoff_);
	std::cout<<"Setting cutoff to: "<<cutoff_<<std::endl;

	// prepare data container:
	data_.setDataSetCount(1);               // one data set for water   
    data_.setColumnCount(0, 5);             // x y z s r

    // add plot module to analysis data:
    int i = 2;
    AnalysisDataLongFormatPlotModulePointer lfplotm(new AnalysisDataLongFormatPlotModule(i));
    const char *poreParticleFileName = poreParticleFileName_.c_str();
    lfplotm -> setFileName(poreParticleFileName);
    lfplotm -> setPrecision(3);
    std::vector<char*> header = {"t", "x", "y", "z", "s", "r"};
    lfplotm -> setHeader(header);
    data_.addModule(lfplotm);  

    // add pdb plot module to analysis data:
    AnalysisDataPdbPlotModulePointer pdbplotm(new AnalysisDataPdbPlotModule(i));
    pdbplotm -> setFileName(poreParticleFileName);
    data_.addModule(pdbplotm);


    // RESIDUE MAPPING DATA
    //-------------------------------------------------------------------------


    // set dataset properties:
    dataResMapping_.setDataSetCount(1);
    dataResMapping_.setColumnCount(0, 6);   // refID s rho phi 
    dataResMapping_.setMultipoint(true);

    // add long format plot module:
    int j = 1;
    AnalysisDataLongFormatPlotModulePointer lfpltResMapping(new AnalysisDataLongFormatPlotModule(j));
    const char *fnResMapping = "res_mapping.dat";
    std::vector<char*> headerResMapping = {"t", "mappedId", "s", "rho", "phi", "poreLining", "poreFacing"};
    lfpltResMapping -> setFileName(fnResMapping);
    lfpltResMapping -> setHeader(headerResMapping);
    lfpltResMapping -> setPrecision(5);    // TODO: different treatment for integers?
    dataResMapping_.addModule(lfpltResMapping);


    // set pdb data set properties:
    dataResMappingPdb_.setDataSetCount(1);
    dataResMappingPdb_.setColumnCount(0, 7);

    // add PDB plot module:
    AnalysisDataPdbPlotModulePointer plotResMappingPdb(new AnalysisDataPdbPlotModule(3));
    plotResMappingPdb -> setFileName("res_mapping.pdb");
    dataResMappingPdb_.addModule(plotResMappingPdb);
    


    // PREPARE SELECTIONS FOR MAPPING
    //-------------------------------------------------------------------------

    // prepare a centre of geometry selection collection:
    poreMappingSelCol_.setReferencePosType("res_cog");
    poreMappingSelCol_.setOutputPosType("res_cog");

    // selection strings:
    // TODO: move this to a config file!
    std::string poreMappingSelCalString = "name CA";
    std::string poreMappingSelCogString = "resname ALA or" 
                                          "resname ARG or"
                                          "resname ASN or"
                                          "resname ASP or"
                                          "resname ASX or"
                                          "resname CYS or"
                                          "resname GLU or"
                                          "resname GLN or"
                                          "resname GLX or"
                                          "resname GLY or"
                                          "resname HIS or"
                                          "resname ILE or"
                                          "resname LEU or"
                                          "resname LYS or"
                                          "resname MET or"
                                          "resname PHE or"
                                          "resname PRO or"
                                          "resname SER or"
                                          "resname THR or"
                                          "resname TRP or"
                                          "resname TYR or"
                                          "resname VAL";
    

    // create selections as defined above:
    poreMappingSelCal_ = poreMappingSelCol_.parseFromString(poreMappingSelCalString)[0];
    poreMappingSelCog_ = poreMappingSelCol_.parseFromString(poreMappingSelCogString)[0];
    poreMappingSelCol_.setTopology(top.topology(), 0);
    poreMappingSelCol_.compile();

    // validate that there is a c-alpha for each residue:
    if( poreMappingSelCal_.posCount() != poreMappingSelCog_.posCount() )
    {
        std::cerr<<"ERROR: Could not find a C-alpha for each residue in pore forming group."
                 <<std::endl<<"Is your pore a protein?"<<std::endl;
        std::abort();
    }

    
    // PREPARE TOPOLOGY QUERIES
    //-------------------------------------------------------------------------

	// load full topology:
	t_topology *topol = top.topology();	

	// access list of all atoms:
	t_atoms atoms = topol -> atoms;
    
	// create atomprop struct:
	gmx_atomprop_t aps = gmx_atomprop_init();

    
    // GET ATOM RADII FROM TOPOLOGY
    //-------------------------------------------------------------------------
	

	// create vector of van der Waals radii and allocate memory:
	vdwRadii_.reserve(atoms.nr);


	// loop over all atoms in system and get vdW-radii:
	for(int i=0; i<atoms.nr; i++)
	{
		real vdwRadius;

		// query vdW radius of current atom:
		if(gmx_atomprop_query(aps, 
		                      epropVDW, 
							  *(atoms.resinfo[atoms.atom[i].resind].name),
							  *(atoms.atomname[i]), &vdwRadius)) 
		{
			// TODO: include scale factor here?
		}
		else
		{
			// could not find vdW radius
			// TODO: handle this case
		}

		// add radius to vector of radii:
		vdwRadii_.push_back(vdwRadius);
	}


	// find largest van der Waals radius in system:
	maxVdwRadius_ = *std::max_element(vdwRadii_.begin(), vdwRadii_.end());


    // TRACK C-ALPHAS and RESIDUE INDECES
    //-------------------------------------------------------------------------
   
    // loop through all atoms, get index lists for c-alphas and residues:
    for(int i = 0; i < atoms.nr; i++)
    {
        // check for calpha:
        if( std::strcmp(*atoms.atomname[i], "CA") == 0 )
        {
           poreCAlphaIndices_.push_back(i); 
        }

        // track residue ID of atoms: 
        residueIndices_.push_back(atoms.atom[i].resind);
        atomResidueMapping_[i] = atoms.atom[i].resind;
        residueAtomMapping_[atoms.atom[i].resind].push_back(i);
    }

    // remove duplicate residue indices:
    std::vector<int>::iterator it;
    it = std::unique(residueIndices_.begin(), residueIndices_.end());
    residueIndices_.resize(std::distance(residueIndices_.begin(), it));

    //
    ConstArrayRef<int> refselAtomIdx = refsel_.atomIndices();
    for(it = residueIndices_.begin(); it != residueIndices_.end(); it++)
    {
        // current residue id:
        int resId = *it;
    
        // get vector of all atom indices in this residue:
        std::vector<int> atomIdx = residueAtomMapping_[resId];

        // for each atom in residue, check if it belongs to pore selection:
        bool addResidue = false;
        std::vector<int>::iterator jt;
        for(jt = atomIdx.begin(); jt != atomIdx.end(); jt++)
        {
            // check if atom belongs to pore selection:
            if( std::find(refselAtomIdx.begin(), refselAtomIdx.end(), *jt) != refselAtomIdx.end() )
            {
                // add atom to list of pore atoms:
                poreAtomIndices_.push_back(*jt);

                // if at least one atom belongs to pore group, the whole residue will be considered:
                addResidue = true;
            }
        }

        // add residue, if at least one atom is part of pore:
        if( addResidue == true )
        {
            poreResidueIndices_.push_back(resId);
        }
    }


    // FINALISE ATOMPROP QUERIES
    //-------------------------------------------------------------------------
    
	// delete atomprop struct:
	gmx_atomprop_destroy(aps);
}




/*
 *
 */
void
trajectoryAnalysis::analyzeFrame(int frnr, const t_trxframe &fr, t_pbc *pbc,
                                 TrajectoryAnalysisModuleData *pdata)
{
	// get data handles for this frame:
	AnalysisDataHandle dh = pdata -> dataHandle(data_);
    AnalysisDataHandle dhResMapping = pdata -> dataHandle(dataResMapping_);

	// get thread-local selection of reference particles:
	const Selection &refSelection = pdata -> parallelSelection(refsel_);

	// get data for frame number frnr into data handle:
    dh.startFrame(frnr, fr.time);
    dhResMapping.startFrame(frnr, fr.time);


    // UPDATE INITIAL PROBE POSITION FOR THIS FRAME
    //-------------------------------------------------------------------------

    // recalculate initial probe position based on reference group COM:
    if( pfInitProbePosIsSet_ == false )
    {  
        // helper variable for conditional assignment of selection:
        Selection tmpsel;
  
        // has a group for specifying initial probe position been set?
        if( ippselIsSet_ == true )
        {
            // use explicitly given selection:
            tmpsel = ippsel_;
        }
        else 
        {
            // default to overall group of pore forming particles:
            tmpsel = refsel_;
        }
     
        // load data into initial position selection:
        const gmx::Selection &initPosSelection = pdata -> parallelSelection(tmpsel);
 
        // initialse total mass and COM vector:
        real totalMass = 0.0;
        gmx::RVec centreOfMass(0.0, 0.0, 0.0);
        
        // loop over all atoms: 
        for(int i = 0; i < initPosSelection.atomCount(); i++)
        {
            // get i-th atom position:
            gmx::SelectionPosition atom = initPosSelection.position(i);

            // add to total mass:
            totalMass += atom.mass();

            // add to COM vector:
            // TODO: implement separate centre of geometry and centre of mass 
            centreOfMass[0] += atom.mass() * atom.x()[0];
            centreOfMass[1] += atom.mass() * atom.x()[1];
            centreOfMass[2] += atom.mass() * atom.x()[2];
        }

        // scale COM vector by total MASS:
        centreOfMass[0] /= 1.0 * totalMass;
        centreOfMass[1] /= 1.0 * totalMass;
        centreOfMass[2] /= 1.0 * totalMass; 

        // set initial probe position:
        pfInitProbePos_[0] = centreOfMass[0];
        pfInitProbePos_[1] = centreOfMass[1];
        pfInitProbePos_[2] = centreOfMass[2];
    }

    // inform user:
    if( debug_output_ == true )
    {
        std::cout<<std::endl
                 <<"Initial probe position for this frame is:  "
                 <<pfInitProbePos_[0]<<", "
                 <<pfInitProbePos_[1]<<", "
                 <<pfInitProbePos_[2]<<". "
                 <<std::endl;
    }


    // GET VDW RADII FOR SELECTION
    //-------------------------------------------------------------------------
    // TODO: Move this to separate class and test!
    // TODO: Should then also work for coarse-grained situations!

	// create vector of van der Waals radii and allocate memory:
    std::vector<real> selVdwRadii;
	selVdwRadii.reserve(refSelection.atomCount());
    std::cout<<"selVdwRadii.size() = "<<selVdwRadii.size()<<std::endl;

	// loop over all atoms in system and get vdW-radii:
	for(int i=0; i<refSelection.atomCount(); i++)
    {
        // get global index of i-th atom in selection:
        gmx::SelectionPosition atom = refSelection.position(i);
        int idx = atom.mappedId();

		// add radius to vector of radii:
		selVdwRadii.push_back(vdwRadii_[idx]);
	}


	// PORE FINDING AND RADIUS CALCULATION
	// ------------------------------------------------------------------------

	// initialise neighbourhood search:
	AnalysisNeighborhoodSearch nbSearch = nb_.initSearch(pbc, refSelection);

   /* 
    std::cout<<"pfMethod = "<<pfMethod_<<std::endl
             <<"pfProbeStepLength = "<<pfProbeStepLength_<<std::endl
             <<"pfProbeRadius = "<<pfProbeRadius_<<std::endl
             <<"pfMaxFreeDist = "<<pfMaxFreeDist_<<std::endl
             <<"pfMaxProbeSteps = "<<pfMaxProbeSteps_<<std::endl
             <<"pfInitProbePos = "<<pfInitProbePos_[0]<<"  "
                                  <<pfInitProbePos_[1]<<"  "
                                  <<pfInitProbePos_[2]<<std::endl
             <<"pfChanDirVec = "<<pfChanDirVec_[0]<<"  "
                                <<pfChanDirVec_[1]<<"  "
                                <<pfChanDirVec_[2]<<std::endl
             <<"saRandomSeed = "<<saRandomSeed_<<std::endl
             <<"saMaxCoolingIter = "<<saMaxCoolingIter_<<std::endl
             <<"saNumCostSamples = "<<saNumCostSamples_<<std::endl
             <<"saXi = "<<saXi_<<std::endl
             <<"saConvRelTol = "<<saConvRelTol_<<std::endl
             <<"saInitTemp = "<<saInitTemp_<<std::endl
             <<"saCoolingFactor = "<<saCoolingFactor_<<std::endl
             <<"saStepLengthFactor = "<<saStepLengthFactor_<<std::endl
             <<"saUseAdaptiveCandGen = "<<saUseAdaptiveCandGen_<<std::endl;
    */


    // create path finding module:
    std::unique_ptr<AbstractPathFinder> pfm;
    if( pfMethod_ == "inplane-optim" )
    {
    	RVec initProbePos(pfInitProbePos_[0], pfInitProbePos_[1], pfInitProbePos_[2]);
    	RVec chanDirVec(pfChanDirVec_[0], pfChanDirVec_[1], pfChanDirVec_[2]);
        pfm.reset(new InplaneOptimisedProbePathFinder(pfProbeStepLength_, pfProbeRadius_, 
                                            pfMaxFreeDist_, pfMaxProbeSteps_, 
                                            initProbePos, chanDirVec, selVdwRadii, 
                                            &nbSearch, saRandomSeed_, 
                                            saMaxCoolingIter_, saNumCostSamples_, 
                                            saXi_, saConvRelTol_, saInitTemp_, 
                                            saCoolingFactor_, saStepLengthFactor_, 
                                            saUseAdaptiveCandGen_));
    }
    else if( pfMethod_ == "optim-direction" )
    {
        std::cout<<"OPTIM-DIRECTION"<<std::endl;

    	RVec initProbePos(pfInitProbePos_[0], pfInitProbePos_[1], pfInitProbePos_[2]);
    	RVec chanDirVec(pfChanDirVec_[0], pfChanDirVec_[1], pfChanDirVec_[2]);
        pfm.reset(new OptimisedDirectionProbePathFinder(pfProbeStepLength_, pfProbeRadius_, 
                                            pfMaxFreeDist_, pfMaxProbeSteps_, 
                                            initProbePos, selVdwRadii, 
                                            &nbSearch, saRandomSeed_, 
                                            saMaxCoolingIter_, saNumCostSamples_, 
                                            saXi_, saConvRelTol_, saInitTemp_, 
                                            saCoolingFactor_, saStepLengthFactor_, 
                                            saUseAdaptiveCandGen_));
    }   
    else if( pfMethod_ == "naive-cylindrical" )
    {
    	RVec initProbePos(pfInitProbePos_[0], pfInitProbePos_[1], pfInitProbePos_[2]);
    	RVec chanDirVec(pfChanDirVec_[0], pfChanDirVec_[1], pfChanDirVec_[2]);
        pfm.reset(new NaiveCylindricalPathFinder(pfProbeStepLength_,
                                                 pfMaxProbeSteps_,
                                                 pfMaxFreeDist_,
                                                 initProbePos,
                                                 chanDirVec));        
    }










    const gmx::Selection &test = pdata -> parallelSelection(refsel_);


    std::cout<<"initProbePos ="<<" "
             <<pfInitProbePos_[0]<<" "
             <<pfInitProbePos_[1]<<" "
             <<pfInitProbePos_[2]<<" "
             <<std::endl;

//    std::cout<<"atomCount = "<<test.atomCount()<<"  "
//             <<std::endl;













//    std::cout<<"================================================="<<std::endl;
//    std::cout<<std::endl;
//    std::cout<<std::endl;


    // run path finding algorithm on current frame:
    std::cout<<"finding permeation pathway ... ";
    clock_t tPathFinding = std::clock();
    pfm -> findPath();
    tPathFinding = (std::clock() - tPathFinding)/CLOCKS_PER_SEC;
    std::cout<<"done in  "<<tPathFinding<<" sec"<<std::endl;


    // retrieve molecular path object:
    std::cout<<"preparing pathway object ... ";
    clock_t tMolPath = std::clock();
    MolecularPath molPath = pfm -> getMolecularPath();
    tMolPath = (std::clock() - tMolPath)/CLOCKS_PER_SEC;
    std::cout<<"done in  "<<tMolPath<<" sec"<<std::endl;


    std::vector<gmx::RVec> pathPoints = molPath.pathPoints();
    std::vector<real> pathRadii = molPath.pathRadii();

    std::fstream pathfile;
    pathfile.open("pathfile.dat", std::fstream::out);
    pathfile<<"x y z r"<<std::endl;
    for(int i = 0; i < pathRadii.size(); i++)
    {
        pathfile<<pathPoints[i][0]<<" "
                <<pathPoints[i][1]<<" "
                <<pathPoints[i][2]<<" "
                <<pathRadii[i]<<std::endl;
    }
    pathfile.close();





    

    std::cout<<std::endl;
    std::cout<<std::endl;
//    std::cout<<"================================================="<<std::endl;


    // reset smart pointer:
    // TODO: Is this necessary and parallel compatible?
    pfm.reset();





    // ADD PATH DATA TO PARALLELISABLE CONTAINER
    //-------------------------------------------------------------------------

    // access path finding module result:
    real extrapDist = 1.0;
    std::vector<real> arcLengthSample = molPath.sampleArcLength(nOutPoints_, extrapDist);
    std::vector<gmx::RVec> pointSample = molPath.samplePoints(arcLengthSample);
    std::vector<real> radiusSample = molPath.sampleRadii(arcLengthSample);
 
    // loop over all support points of path:
    for(int i = 0; i < nOutPoints_; i++)
    {
        // add to container:
        dh.setPoint(0, pointSample[i][0]);     // x
        dh.setPoint(1, pointSample[i][1]);     // y
        dh.setPoint(2, pointSample[i][2]);     // z
        dh.setPoint(3, arcLengthSample[i]);    // s
        dh.setPoint(4, radiusSample[i]);       // r

        dh.finishPointSet(); 
    }
  

    // WRITE PORE TO OBJ FILE
    //-------------------------------------------------------------------------

    MolecularPathObjExporter molPathExp;
    molPathExp("pore.obj",
               molPath);


    // MAP PORE PARTICLES ONTO PATHWAY
    //-------------------------------------------------------------------------

    std::cout<<std::endl;

    // evaluate pore mapping selection for this frame:
    t_trxframe frame = fr;
    poreMappingSelCol_.evaluate(&frame, pbc);
    const gmx::Selection poreMappingSelCal = pdata -> parallelSelection(poreMappingSelCal_);    
    const gmx::Selection poreMappingSelCog = pdata -> parallelSelection(poreMappingSelCog_);    

    // map pore residue COG onto pathway:
    std::cout<<"mapping pore residue COG onto pathway ... ";
    clock_t tMapResCog = std::clock();
    std::map<int, gmx::RVec> poreCogMappedCoords = molPath.mapSelection(poreMappingSelCog, pbc);
    tMapResCog = (std::clock() - tMapResCog)/CLOCKS_PER_SEC;
    std::cout<<"mapped "<<poreCogMappedCoords.size()
             <<" particles in "<<1000*tMapResCog<<" ms"<<std::endl;

    // map pore residue C-alpha onto pathway:
    std::cout<<"mapping pore residue C-alpha onto pathway ... ";
    clock_t tMapResCal = std::clock();
    std::map<int, gmx::RVec> poreCalMappedCoords = molPath.mapSelection(poreMappingSelCal, pbc);
    tMapResCal = (std::clock() - tMapResCal)/CLOCKS_PER_SEC;
    std::cout<<"mapped "<<poreCalMappedCoords.size()
             <<" particles in "<<1000*tMapResCal<<" ms"<<std::endl;

    
    // check if particles are pore-lining:
    std::cout<<"checking which residues are pore-lining ... ";
    clock_t tResPoreLining = std::clock();
    std::map<int, bool> poreLining = molPath.checkIfInside(poreCogMappedCoords, poreMappingMargin_);
    int nPoreLining = 0;
    std::map<int, bool>::iterator jt;
    for(jt = poreLining.begin(); jt != poreLining.end(); jt++)
    {
        if( jt -> second == true )
        {
            nPoreLining++;
        }
    }
    tResPoreLining = (std::clock() - tResPoreLining)/CLOCKS_PER_SEC;
    std::cout<<"found "<<nPoreLining<<" pore lining residues in "
             <<1000*tResPoreLining<<" ms"<<std::endl;

    // check if residues are pore-facing:
    // TODO: make this conditional on whether C-alphas are available
    std::cout<<"checking which residues are pore-facing ... ";
    clock_t tResPoreFacing = std::clock();
    std::map<int, bool> poreFacing;
    int nPoreFacing = 0;
    std::map<int, gmx::RVec>::iterator it;
    for(it = poreCogMappedCoords.begin(); it != poreCogMappedCoords.end(); it++)
    {
        if( it -> second[1] < poreCalMappedCoords[it->first][1] )
        {
            poreFacing[it->first] = true;
            nPoreFacing++;
        }
        else
        {
            std::cout<<"r_cal = "<<poreCalMappedCoords[it->first][1]<<"  "
                     <<"r_cog = "<<it->second[1]<<std::endl;

            poreFacing[it->first] = false;            
        }
    }
    tResPoreFacing = (std::clock() - tResPoreFacing)/CLOCKS_PER_SEC;
    std::cout<<"found "<<nPoreFacing<<" pore facing residues in "
             <<1000*tResPoreFacing<<" ms"<<std::endl;

    // add points inside to data frame:
    for(it = poreCogMappedCoords.begin(); it != poreCogMappedCoords.end(); it++)
    {
        SelectionPosition pos = poreMappingSelCog.position(it->first);
        
        // add points to dataset:
        dhResMapping.setPoint(0, pos.mappedId());                    // refId
        dhResMapping.setPoint(1, it -> second[0]); // s
        dhResMapping.setPoint(2, it -> second[1]); // rho
        dhResMapping.setPoint(3, it -> second[2]);// phi
        dhResMapping.setPoint(4, poreLining[it -> first]);             // poreLining
        dhResMapping.setPoint(5, poreFacing[it -> first]);             // poreFacing
        dhResMapping.finishPointSet();
    }


    // FINISH FRAME
    //-------------------------------------------------------------------------

    std::cout<<std::endl;

	// finish analysis of current frame:
    dh.finishFrame();
    dhResMapping.finishFrame();
}



/*
 *
 */
void
trajectoryAnalysis::finishAnalysis(int /*nframes*/)
{

}




void
trajectoryAnalysis::writeOutput()
{
    std::cout<<"datSetCount = "<<data_.dataSetCount()<<std::endl
             <<"columnCount = "<<data_.columnCount()<<std::endl
             <<"frameCount = "<<data_.frameCount()<<std::endl
             <<std::endl;
}


