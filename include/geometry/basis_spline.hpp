#ifndef BASIS_SPLINE_HPP
#define BASIS_SPLINE_HPP

#include <vector>

#include <gromacs/utility/real.h> 

/*!
 * \brief Functor class to evaluate basis splines.
 *
 * This class serves as a functor for evaluating the \f$ i \f$-th basis spline 
 * of degree \f$ k \f$ over a knot vector \f$ \mathbf{t} \f$ at a given 
 * evaluation point \f$ x \f$. This is accomplished by means of the Cox-de-Boor
 * recursion, where a basis spline of degree \f$ k > 0 \f$ is written as the 
 * sum of two lower order basis splines:
 *
 * \f[
 *      B_{i,k}(x) = \frac{x - t_i}{t_{i+k} - t_i}B_{i,k-1}(x) + 
 *                   \frac{t_{i + k + 1} - x}{t_{i + k + 1} - t_{i+1}}B_{i+1, k-1}(x)
 * \f]
 *
 * The bottom of the recursion is reached when \f$ k = 0 \f$, where the basis
 * is given by piecewise constant functions of the following form:
 *
 * \f[
 *      B_{i, k}(x) = \begin{cases}
 *                       1, \quad \text{if} ~ t_i \leq x < t_{i+1} \\
 *                       0, \quad \text{otherwise} \\
 *                    \end{cases}
 * \f]
 *
 * Note that in the above recurrence relation, a zero division is defined as
 * \f$ 0/0 = 0 \f$.
 */
class BasisSpline
{
    public:
        
        // constructor and destructor:
        BasisSpline();
        ~BasisSpline();

        // evaluation function and operator:
        real evaluate(std::vector<real> &knotVector, 
                      int degree,
                      int interval,
                      real &evalPoint);
        real operator()(std::vector<real> &knotVector,
                        int degree,
                        int interval,
                        real &evalPoint);

    private:

        // state variables:
        real evalPoint_;
        std::vector<real> knotVector_;

        // bspline recursion:
        real recursion(int k, int i);
};


/*!
 * \brief Functor class to evaluate basis spline derivatives.
 *
 * This functor evaluates the derivative of a basis spline of degree \f$ k \f$
 * over a knot vector \f$ \mathbf{t} \f$ at a given evaluation point \f$ x \f$.
 * It makes use of the recurrence relation
 *
 * \f[
 *      \frac{d}{dx} B_{i, k}(x) = k \left( \frac{B_{i, k-1}(x)}{t_{i + k} - t_i}
 *                               - \frac{B_{i+1, k-1}(x)}{t_{i+k+1} - t_{i+1}} \right)
 * \f]
 *
 * which can is applied to itself in order to evaluate higher order 
 * derivatives. Evaluation of the basis spline functions themselves is handled 
 * by the BasisSpline functor.
 */
class BasisSplineDerivative
{
    public:

        // constructor and destructor:
        BasisSplineDerivative();
        ~BasisSplineDerivative();

        // evaluation function and operator:
        real evaluate(std::vector<real> &knotVector,
                      int degree,
                      int interval,
                      real &evalPoint,
                      unsigned int derivOrder);
        real operator()(std::vector<real> &knotVector,
                        int degree,
                        int interval, 
                        real &evalPoint,
                        unsigned int derivOrder);
};

#endif

