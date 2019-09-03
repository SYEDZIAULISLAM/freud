// Copyright (c) 2010-2019 The Regents of the University of Michigan
// This file is from the freud project, released under the BSD 3-Clause License.

#ifndef STEINHARDT_H
#define STEINHARDT_H

#include <ThreadStorage.h>
#include <complex>
#include <memory>
#include <tbb/tbb.h>

#include "Box.h"
#include "NeighborList.h"
#include "NeighborQuery.h"
#include "VectorMath.h"
#include "fsph/src/spherical_harmonics.hpp"
#include "ThreadStorage.h"
#include "Wigner3j.h"

/*! \file Steinhardt.h
    \brief Computes variants of Steinhardt order parameters.
*/

namespace freud { namespace order {

//! Compute the Steinhardt local rotationally invariant Ql or Wl order parameter for a set of points
/*!
 * Implements the rotationally invariant Ql or Wl order parameter described
 * by Steinhardt. For a particle i, we calculate the average Q_l by summing
 * the spherical harmonics between particle i and its neighbors j in a local
 * region:
 * \f$ \overline{Q}_{lm}(i) = \frac{1}{N_b} \displaystyle\sum_{j=1}^{N_b}
 * Y_{lm}(\theta(\vec{r}_{ij}),\phi(\vec{r}_{ij})) \f$
 *
 * This is then combined in a rotationally invariant fashion to remove local
 * orientational order as follows:
 * \f$ Q_l(i)=\sqrt{\frac{4\pi}{2l+1} \displaystyle\sum_{m=-l}^{l} |\overline{Q}_{lm}|^2 }  \f$
 *
 * If the average flag is set, the order parameters averages over the second neighbor shell.
 * For a particle i, we calculate the average Q_l by summing the spherical
 * harmonics between particle i and its neighbors j and the neighbors k of
 * neighbor j in a local region.
 *
 * If the norm flag is set, the Ql value is normalized by the average Qlm value
 * for the system.
 *
 * If the flag Wl is set, the third-order invariant Wl order parameter will
 * be calculated. Wl can aid in distinguishing between FCC, HCP, and BCC.
 *
 * For more details see:
 * - PJ Steinhardt (1983) (DOI: 10.1103/PhysRevB.28.784)
 * - Wolfgang Lechner (2008) (DOI: 10.1063/Journal of Chemical Physics 129.114707)
 */

class Steinhardt
{
public:
    //! Steinhardt Class Constructor
    /*! Constructor for Steinhardt analysis class.
     *  \param r_max Cutoff radius for running the local order parameter.
     *              Values near first minima of the rdf are recommended.
     *  \param l Spherical harmonic number l.
     *           Must be a positive number.
     *  \param r_min (optional) Lower bound for computing the local order parameter.
     *                         Allows looking at, for instance, only the second shell,
     *                         or some other arbitrary rdf region.
     */
    Steinhardt(float r_max, unsigned int l, float r_min = 0, bool average = false, bool Wl = false, bool weighted = false)
        : m_Np(0), m_r_max(r_max), m_l(l), m_r_min(r_min), m_average(average), m_Wl(Wl), m_weighted(weighted), m_Qlm_local(2 * l + 1)
    {
        // Error Checking
        if (m_r_max < 0.0f || m_r_min < 0.0f)
            throw std::invalid_argument("Steinhardt requires r_min and r_max must be positive.");
        if (m_r_min >= m_r_max)
            throw std::invalid_argument("Steinhardt requires r_min must be less than r_max.");
        if (m_l < 2)
            throw std::invalid_argument("Steinhardt requires l must be two or greater.");
    }

    //! Empty destructor
    virtual ~Steinhardt() {};

    //! Get the number of particles used in the last compute
    unsigned int getNP()
    {
        return m_Np;
    }

    //! Get the last calculated order parameter
    std::shared_ptr<float> getOrder()
    {
        if (m_Wl)
        {
            return m_Wli;
        }
        else if (m_average)
        {
            return m_QliAve;
        }
        else
        {
            return m_Qli;
        }
    }

    //! Get the last calculated Ql
    std::shared_ptr<float> getQl()
    {
        if (m_average)
        {
            return m_QliAve;
        }
        else
        {
            return m_Qli;
        }
    }

    //! Get norm
    float getNorm()
    {
        return m_norm;
    }

    //!< Whether to take a second shell average
    bool isAverage()
    {
        return m_average;
    }

    //!< Whether to use the third-order invariant Wl
    bool isWl()
    {
        return m_Wl;
    }

    //!< Whether to use neighbor weights in computing Qlmi
    bool isWeighted()
    {
        return m_weighted;
    }

    //! Compute the order parameter
    virtual void compute(const freud::locality::NeighborList* nlist,
                                  const freud::locality::NeighborQuery* points, freud::locality::QueryArgs qargs);

private:
    //! \internal
    //! helper function to reduce the thread specific arrays into one array
    void reduce();

    //! Spherical harmonics calculation for Ylm filling a
    //  vector<complex<float> > with values for m = -l..l.
    virtual void computeYlm(const float theta, const float phi, std::vector<std::complex<float>>& Ylm);

    template<typename T> std::shared_ptr<T> makeArray(size_t size);

    //! Reallocates only the necessary arrays when the number of particles changes
    // unsigned int Np number of particles
    void reallocateArrays(unsigned int Np);

    //! Calculates the base Ql order parameter before further modifications
    // if any.
    void baseCompute(const freud::locality::NeighborList* nlist,
                                  const freud::locality::NeighborQuery* points, freud::locality::QueryArgs qargs);

    //! Calculates the neighbor average Ql order parameter
    void computeAve(const freud::locality::NeighborList* nlist,
                                  const freud::locality::NeighborQuery* points, freud::locality::QueryArgs qargs);

    //! Normalize the order parameter
    float normalize();

    //! Sum over Wigner 3j coefficients to compute third-order invariants
    //  Wl from second-order invariants Ql
    void aggregateWl(std::shared_ptr<float> target,
                     std::shared_ptr<std::complex<float>> source);

    // Member variables used for compute
    unsigned int m_Np; //!< Last number of points computed
    float m_r_max;      //!< Maximum r at which to determine neighbors
    unsigned int m_l;  //!< Spherical harmonic l value.
    float m_r_min;      //!< Minimum r at which to determine neighbors (default 0)

    // Flags
    bool m_average; //!< Whether to take a second shell average (default false)
    bool m_Wl;      //!< Whether to use the third-order invariant Wl (default false)
    bool m_weighted;      //!< Whether to use neighbor weights in computing Qlmi (default false)

    std::shared_ptr<std::complex<float>> m_Qlmi; //!< Qlm for each particle i
    std::shared_ptr<std::complex<float>> m_Qlm;  //!< Normalized Qlm(Ave) for the whole system
    util::ThreadStorage<std::complex<float>> m_Qlm_local; //!< Thread-specific m_Qlm(Ave)
    std::shared_ptr<float> m_Qli;              //!< Ql locally invariant order parameter for each particle i
    std::shared_ptr<float> m_QliAve;           //!< Averaged Ql with 2nd neighbor shell for each particle i
    std::shared_ptr<complex<float>> m_QlmiAve; //!< Averaged Qlm with 2nd neighbor shell for each particle i
    std::shared_ptr<std::complex<float>> m_QlmAve;   //!< Normalized QlmiAve for the whole system
    float m_norm;                                    //!< System normalized order parameter
    std::shared_ptr<float> m_Wli; //!< Wl order parameter for each particle i, also used for Wl averaged data
};

}; };  // end namespace freud::order
#endif // STEINHARDT_H
