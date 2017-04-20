#include <iostream>

#include "geometry/abstract_cubic_spline_interp.hpp"


/*
 * Constructor.
 */
AbstractCubicSplineInterp::AbstractCubicSplineInterp()
{

}


/*
 * Destructor.
 */
AbstractCubicSplineInterp::~AbstractCubicSplineInterp()
{

}


/*
 *
 */
void
AbstractCubicSplineInterp::assembleDiagonals(std::vector<real> &knotVector,
                                             std::vector<real> &x,
                                             real *subDiag,
                                             real *mainDiag,
                                             real *superDiag,
                                             eSplineInterpBoundaryCondition bc)
{
    // dimension of system:
    int nDat = x.size();
    int nSys = nDat + 2;

    // initialise basis spline (derivative) functor:
    BasisSpline B;
    BasisSplineDerivative D;

    // handle boundary conditions:
    if( bc == eSplineInterpBoundaryHermite )
    {
        int firstOrderDeriv = 1;
        real xLo = x.front();
        real xHi = x.back();

        // lower boundary:
        mainDiag[0] = D(knotVector, degree_, 0, xLo, firstOrderDeriv);
        superDiag[0] = D(knotVector, degree_, 1, xLo, firstOrderDeriv);
 
        // higher boundary:
        mainDiag[nSys - 1] = D(knotVector, degree_, nSys - 1, xHi, firstOrderDeriv);
        subDiag[nSys - 2] = D(knotVector, degree_, nSys - 2, xHi, firstOrderDeriv);
    }
    else if( bc == eSplineInterpBoundaryNatural )
    {
        std::cerr<<"ERROR: Only Hermite boundary conditions implemented!"<<std::endl;
        std::abort();
    }
    else
    {
        std::cerr<<"ERROR: Only Hermite boundary conditions implemented!"<<std::endl;
        std::abort();
    }

    // assemble subdiagonal:
    for(int i = 0; i < nDat; i++)
    {
        subDiag[i] = B(knotVector, degree_, i, x.at(i));
    }

    // assemble main diagonal:
    for(int i = 0; i < nDat; i++)
    {
        mainDiag[i + 1] = B(knotVector, degree_, i+1, x.at(i));
    }

    // assemble superdiagonal:
    for(int i = 1; i < nSys - 1; i++)
    {
        superDiag[i] = B(knotVector, degree_, i + 1, x.at(i - 1));
    }
}


/*
 *
 */
void
AbstractCubicSplineInterp::assembleRhs(std::vector<real> &x,
                                       std::vector<real> &f,
                                       real *rhsVec,
                                       eSplineInterpBoundaryCondition bc)
{
    // get system size:
    int nDat = x.size();
    int nSys = nDat + 2;

    // handle bondary conditions:
    if( bc == eSplineInterpBoundaryHermite )
    {
        // lower boundary:
        rhsVec[0] = estimateEndpointDeriv(x, 
                                          f,
                                          eSplineInterpEndpointLo,
                                          eSplineInterpDerivParabolic);

        // higher boundary:
        rhsVec[nSys - 1] = estimateEndpointDeriv(x, 
                                                 f, 
                                                 eSplineInterpEndpointHi,
                                                 eSplineInterpDerivParabolic);
    }
    else if( bc == eSplineInterpBoundaryNatural )
    {
        // TODO: implement this
        std::cerr<<"ERROR: Only Hermite boundary conditions implemented!"<<std::endl;
        std::abort();
 
        // lower boundary:
        rhsVec[0] = 0.0;

        // higher boundary:
        rhsVec[nSys - 1] = 0.0; 
    }   
    else
    {
        std::cerr<<"ERROR: Only Hermite boundary conditions are implemented!"<<std::endl;
        std::abort();
    }

    // assemble internal points:
    for(int i = 0; i < nDat; i++)
    {
        rhsVec[i + 1] = f.at(i);
    }
}


/*
 * Internal helper function for creating a knot vector from a vector of input 
 * data. The knot vector is essentially a copy of the data vector with its 
 * first and last element repeated four times.
 */
std::vector<real> 
AbstractCubicSplineInterp::prepareKnotVector(std::vector<real> &x)
{
    // initialise knot vector:
    std::vector<real> knotVector;

    // add repeats of first knot:
    for(int i = 0; i < degree_; i++)
    {
        knotVector.push_back(x.front());
    }

    // copy support points:
    for(int i = 0; i < x.size(); i++)
    {
        knotVector.push_back(x.at(i));
    }

    // add repeats of last knot:
    for(int i = 0; i < degree_; i++)
    {
        knotVector.push_back(x.back());
    }

    // return knot vector:
    return knotVector;
}


/*
 * This function estimates the derivatives (of the data) at the endpoints of
 * the given data range. Estimation can be done with a simple finite difference
 * or via a parabolic fit with a ghost node assumption.
 */
real
AbstractCubicSplineInterp::estimateEndpointDeriv(std::vector<real> &x,
                                                 std::vector<real> &f,
                                                 eSplineInterpEndpoint endpoint,
                                                 eSplineInterpDerivEstimate method)
{
    // get overall number of data points:
    unsigned int nDat = x.size();

    // which finite difference should be used?
    if( method == eSplineInterpDerivParabolic )
    {
        // declare helper variables for local points:
        real xDeltaLo;
        real xDeltaHi;
        real fDeltaLo;
        real fDeltaHi;

        // estimate derivative at lower or higher endpoint?
        if( endpoint == eSplineInterpEndpointLo )
        {
            xDeltaLo = x.at(0) - x.at(2);
            xDeltaHi = x.at(1) - x.at(0);
            fDeltaLo = (f.at(0) - f.at(2))/xDeltaLo;
            fDeltaHi = (f.at(1) - f.at(0))/xDeltaHi;
        }
        else if( endpoint == eSplineInterpEndpointHi )
        {
            xDeltaLo = x.at(nDat - 1) - x.at(nDat - 2); 
            xDeltaHi = x.at(nDat - 3) - x.at(nDat - 1);
            fDeltaLo = (f.at(nDat - 1) - f.at(nDat - 2))/xDeltaLo;
            fDeltaHi = (f.at(nDat - 3) - f.at(nDat - 1))/xDeltaHi;
        }

        // parabolic estimate of endpoint derivative:
        return (xDeltaLo*fDeltaHi + xDeltaHi*fDeltaLo)/(xDeltaLo + xDeltaHi);
    }
    else 
    {
        // declare helper variables for local points:
        real fHi;
        real fLo;
        real xHi;
        real xLo;

        // estimate derivative at lower or upper endpoint?
        if( endpoint == eSplineInterpEndpointLo )
        {  
           fHi = f.at(1);
           xHi = x.at(1);
           fLo = f.at(0);
           xLo = f.at(0);
        }
        else if( endpoint == eSplineInterpEndpointHi )
        {
           fHi = f.at(nDat - 1);
           xHi = x.at(nDat - 1);
           fLo = f.at(nDat - 2);
           xLo = f.at(nDat - 2);        
        }

        // simple estimate of endpoint derivative:
        return (fHi - fLo)/(xHi - xLo);
    }
}

