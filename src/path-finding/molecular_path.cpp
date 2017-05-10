#include <iostream>

#include <gromacs/pbcutil/pbc.h>
#include <gromacs/selection/nbsearch.h>
#include <gromacs/selection/selection.h>

#include "geometry/cubic_spline_interp_1D.hpp"
#include "geometry/cubic_spline_interp_3D.hpp"
#include "geometry/spline_curve_1D.hpp"
#include "geometry/spline_curve_3D.hpp"

#include "path-finding/molecular_path.hpp"


/*
 * Constructor.
 */
MolecularPath::MolecularPath(std::vector<gmx::RVec> &pathPoints, 
                             std::vector<real> &pathRadii)
    : centreLine_()
    , poreRadius_()
{
    std::cout<<"mol path constructor"<<std::endl;

    // assign internal containers for original path data:
    pathPoints_ = pathPoints;
    pathRadii_ = pathRadii;

    // construct centre line spline by interpolation of path points:
    CubicSplineInterp3D Interp3D;
    centreLine_ = Interp3D(pathPoints_, eSplineInterpBoundaryHermite);

    // get arc length at original control points:
    std::vector<real> arcLen = centreLine_.ctrlPointArcLength();
    openingLo_ = arcLen.front();
    openingHi_ = arcLen.back();
    length_ = std::abs(openingHi_ - openingLo_);

    // interpolate radius:
    CubicSplineInterp1D Interp1D;
    poreRadius_ =  Interp1D(arcLen, pathRadii_, eSplineInterpBoundaryHermite);

    // reparameterise centre line spline by arc length:
    centreLine_.arcLengthParam();


    
    std::cout<<"end mol path constructor"<<std::endl;
}


/*
 * Destructor.
 */
MolecularPath::~MolecularPath()
{

}


/*
 * Maps all positions onto molecular pathway.
 */
std::map<int, gmx::RVec>
MolecularPath::mapSelection(gmx::Selection mapSel,
                            t_pbc *nbhSearchPbc)
{
    // create a set of reference positions on the pore centre line:
    // TODO: fine grain the reference point set
    gmx::AnalysisNeighborhoodPositions centreLinePos(pathPoints_);

    // prepare neighborhood search:
    real nbhSearchCutoff = 0.2;
    real mapTol = 1e-1;
    gmx::AnalysisNeighborhood nbh;
    nbh.setCutoff(nbhSearchCutoff);

    // create a neighborhood search with centre line points as reference:
    gmx::AnalysisNeighborhoodSearch nbhSearch = nbh.initSearch(nbhSearchPbc, centreLinePos);

    // build map of pathway mapped coordinates:
    std::map<int, gmx::RVec> mappedCoords;
    for(int i = 0; i < mapSel.posCount(); i++)
    {
        // find closest reference point on centre line:
        gmx::AnalysisNeighborhoodPair pair = nbhSearch.nearestPoint(mapSel.position(i));

        // check if reference point was found within cutoff distance:
        if( pair.isValid() == false )
        {
            continue;
        }

        // refine mapping:
        gmx::RVec mappedCoord = centreLine_.cartesianToCurvilinear(mapSel.position(i).x(),
                                                                   pair.refIndex(),
                                                                   mapTol);
        
        // TODO: check if this is within the local pore radius!

        // add to list of mapped coordinates:
        mappedCoords[mapSel.position(i).refId()] = mappedCoord;
    }

    return mappedCoords;
}


/*
 * Checks if points described by a set of mapped coordinates lie within the 
 * pre radius.
 */
std::map<int, bool>
MolecularPath::checkIfInside(std::map<int, gmx::RVec> mappedCoords)
{
    // create map for check results:
    std::map<int, bool> isInside;

    std::map<int, gmx::RVec>::iterator it;
    for(it = mappedCoords.begin(); it != mappedCoords.end(); it++)
    {
        real evalPoint = it -> second[0];
        isInside[it -> first] = (it -> second[1] < poreRadius_(evalPoint, 0, eSplineEvalDeBoor));
    }

    // return assessment:
    return isInside;
}



/*
 * Returns length of the pathway, defined as the the arc length distance 
 * between the first and last control point
 */
real
MolecularPath::length()
{
    return length_;
}



/*
 * Returns a vector of equally spaced arc length points that extends a 
 * specified distance beyond the openings of the pore.
 */
std::vector<real>
MolecularPath::sampleArcLength(int nPoints,
                               real extrapDist)
{
    // get spacing of points in arc length:
    real arcLenStep = sampleArcLenStep(nPoints, extrapDist);

    // evaluate spline to obtain sample points:
    std::vector<real> arcLengthSample;
    arcLengthSample.reserve(nPoints);
    for(int i = 0; i < nPoints; i++)
    {
        // calculate evaluation point:
        arcLengthSample.push_back( openingLo_ - extrapDist + i*arcLenStep );  
    }

    // return vector of points:
    return arcLengthSample;
}



/*
 * Returns a vector of point on the molecular path's centre line. The points 
 * will be equally spaced in arc length and sampling will extend beyond the 
 * pore openings for the specified distance.
 */
std::vector<gmx::RVec>
MolecularPath::samplePoints(int nPoints,
                            real extrapDist)
{
    // sample equidistant arc length values:
    std::vector<real> arcLengthSteps = sampleArcLength(nPoints, extrapDist);

    // return vector of points at these values:
    return samplePoints(arcLengthSteps);
}


/*
 * Returns a vector of points on the path's centre line at the given arc length
 * parameter values.
 */
std::vector<gmx::RVec>
MolecularPath::samplePoints(std::vector<real> arcLengthSample)
{
    // evaluate spline to obtain sample points:
    std::vector<gmx::RVec> points;
    points.reserve(arcLengthSample.size());
    for(int i = 0; i < arcLengthSample.size(); i++)
    {
        // evaluate spline at this point:
        points.push_back( centreLine_(arcLengthSample[i], 0, eSplineEvalDeBoor) );
    }

    // return vector of points:
    return points;
}


/*
 *
 */
std::vector<gmx::RVec>
MolecularPath::sampleTangents(int nPoints, real extrapDist)
{
    std::cout<<"sample tangents"<<std::endl;

    // sample equidistant arc length values:
    std::vector<real> arcLengthSteps = sampleArcLength(nPoints, extrapDist);

    // return vector of tangents at these values:
    return sampleTangents(arcLengthSteps);
}


/*
 *
 */
std::vector<gmx::RVec>
MolecularPath::sampleTangents(std::vector<real> arcLengthSample)
{
    std::cout<<"sample tangents inner"<<std::endl;
    std::cout<<"size = "<<arcLengthSample.size()<<std::endl;
    std::cout<<" pathPoints_.size = "<<pathPoints_.size()<<std::endl;
    
    // evaluate spline to obtain sample points:
    std::vector<gmx::RVec> tangents;
//    tangents.reserve(arcLengthSample.size());
    for(int i = 0; i < arcLengthSample.size(); i++)
    {
        std::cout<<" i = "<<i<<std::endl;

        // evaluate spline at this point:
        tangents.push_back( centreLine_.tangentVec(arcLengthSample[i]) );
    }

    std::cout<<"sample tangents end"<<std::endl;

    // return vector of points:
    return tangents;
}


/*
 *
 */
std::vector<gmx::RVec>
MolecularPath::sampleNormals(int nPoints, real extrapDist)
{
    // sample equidistant arc length values:
    std::vector<real> arcLengthSteps = sampleArcLength(nPoints, extrapDist);

    // return vector of normals at these values:
    return sampleNormals(arcLengthSteps);
}


/*
 * TODO: implement this
 */
std::vector<gmx::RVec>
MolecularPath::sampleNormals(std::vector<real> arcLengthSample)
{
    // evaluate spline to obtain sample points:
    std::vector<gmx::RVec> normals;

    // return vector of points:
    return normals;
}


/*
 * Returns a vector of radius values at equally spaced points a long the path.
 * Sampling extends the specified distance beyond the openings of the pore.
 */
std::vector<real>
MolecularPath::sampleRadii(int nPoints,
                           real extrapDist)
{
    // get spacing of points in arc length:
    real arcLenStep = sampleArcLenStep(nPoints, extrapDist);

    // evaluate spline to obtain sample points:
    std::vector<real> radii;
    for(int i = 0; i < nPoints; i++)
    {
        // calculate evaluation point:
        real evalPoint = openingLo_ - extrapDist + i*arcLenStep;  

        // evaluate spline at this point:
        radii.push_back( poreRadius_(evalPoint, 0, eSplineEvalDeBoor) );
    }

    // return vector of points:
    return radii;
}


/*
 * Returns a vector of radius values at the given arc length parameter values.
 */
std::vector<real>
MolecularPath::sampleRadii(std::vector<real> arcLengthSample)
{
    // evaluate spline to obtain sample points:
    std::vector<real> radii;
    for(int i = 0; i < arcLengthSample.size(); i++)
    {
        // evaluate spline at this point:
        radii.push_back( poreRadius_(arcLengthSample[i], 0, eSplineEvalDeBoor) );
    }

    // return vector of points:
    return radii;
}


/*
 *
 */
real
MolecularPath::sampleArcLenStep(int nPoints, real extrapDist)
{
    // get spacing of points in arc length:
    return (this -> length() + 2.0*extrapDist)/(nPoints - 1);
}













