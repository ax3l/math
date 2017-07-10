// Copyright Nick Thompson, 2017
// Use, modification and distribution are subject to the
// Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_MATH_QUADRATURE_DETAIL_TANH_SINH_DETAIL_HPP
#define BOOST_MATH_QUADRATURE_DETAIL_TANH_SINH_DETAIL_HPP

#include <cmath>
#include <vector>
#include <typeinfo>
#include <boost/math/constants/constants.hpp>
#include <boost/math/special_functions/next.hpp>


namespace boost{ namespace math{ namespace quadrature { namespace detail{


// Returns the tanh-sinh quadrature of a function f over the open interval (-1, 1)

template<class Real, class Policy>
class tanh_sinh_detail 
{
   static const int initializer_selector =
      !std::numeric_limits<Real>::is_specialized || (std::numeric_limits<Real>::radix != 2) ?
      0 :
      (std::numeric_limits<Real>::digits < 30) && (std::numeric_limits<Real>::max_exponent <= 128) ?
      1 : 0;
public:
    tanh_sinh_detail(Real tol, size_t max_refinements, std::size_t initial_commit = 4) : m_tol(tol), m_max_refinements(max_refinements), m_committed_refinements(initial_commit)
    {
       typedef mpl::int_<initializer_selector> tag_type;
       init(tag_type());
    }

    template<class F>
    Real integrate(const F f, Real* error, Real* L1, const char* function, Real left_min_complement = tools::epsilon<Real>(), Real right_min_complement = tools::epsilon<Real>()) const;

private:
   const std::vector<Real>& get_abscissa_row(std::size_t n)const
   {
      if (m_committed_refinements < n)
         extend_refinements();
      BOOST_ASSERT(m_committed_refinements >= n);
      return m_abscissas[n];
   }
   const std::vector<Real>& get_weight_row(std::size_t n)const
   {
      if (m_committed_refinements < n)
         extend_refinements();
      BOOST_ASSERT(m_committed_refinements >= n);
      return m_weights[n];
   }
   std::size_t get_first_complement_index(std::size_t n)const
   {
      if (m_committed_refinements < n)
         extend_refinements();
      BOOST_ASSERT(m_committed_refinements >= n);
      return m_first_complements[n];
   }

   void init(const mpl::int_<0>&);
   void init(const mpl::int_<1>&);

   void extend_refinements()const
   {
      ++m_committed_refinements;
      std::size_t row = m_committed_refinements;
      Real h = ldexp(Real(1), -static_cast<int>(row));
      std::size_t first_complement = 0;
      for (Real pos = h; pos < m_t_max; pos += 2 * h)
      {
         if (pos < m_t_crossover)
            ++first_complement;
         m_abscissas[row].push_back(pos < m_t_crossover ? abscissa_at_t(pos) : -abscissa_complement_at_t(pos));
      }
      m_first_complements[row] = first_complement;
      for (Real pos = h; pos < m_t_max; pos += 2 * h)
         m_weights[row].push_back(weight_at_t(pos));
   }

   static inline Real abscissa_at_t(const Real& t)
   {
      using std::tanh;
      using std::sinh;
      return tanh(constants::half_pi<Real>()*sinh(t));
   }
   static inline Real weight_at_t(const Real& t)
   {
      using std::cosh;
      using std::sinh;
      Real cs = cosh(constants::half_pi<Real>() * sinh(t));
      return constants::half_pi<Real>() * cosh(t) / (cs * cs);
   }
   static inline Real abscissa_complement_at_t(const Real& t)
   {
      using std::cosh;
      using std::exp;
      using std::sinh;
      Real u2 = constants::half_pi<Real>() * sinh(t);
      return 1 / (exp(u2) *cosh(u2));
   }
   static inline Real t_from_abscissa_complement(const Real& x)
   {
      using std::log;
      using std::sqrt;
      Real l = log(sqrt((2 - x) / x));
      return log((sqrt(4 * l * l + constants::pi<Real>() * constants::pi<Real>()) + 2 * l) / constants::pi<Real>());
   };

   
   mutable std::vector<std::vector<Real>> m_abscissas;
   mutable std::vector<std::vector<Real>> m_weights;
   mutable std::vector<std::size_t>       m_first_complements;
   std::size_t                       m_max_refinements, m_inital_row_length;
   mutable std::size_t               m_committed_refinements;
   Real m_tol, m_t_max, m_t_crossover;
};

template<class Real, class Policy>
template<class F>
Real tanh_sinh_detail<Real, Policy>::integrate(const F f, Real* error, Real* L1, const char* function, Real left_min_complement, Real right_min_complement) const
{
    using std::abs;
    using std::floor;
    using std::tanh;
    using std::sinh;
    using std::sqrt;
    using boost::math::constants::half;
    using boost::math::constants::half_pi;
    //
    // We maintain 4 integer values:
    // max_left_position is the logical index of the abscissa value closest to the
    // left endpoint of the range that we can call f(x_i) on without rounding error
    // inside f(x_i) causing evaluation at the endpoint.
    // max_left_index is the actual position in the current row that has a logical index
    // no higher than max_left_position.  Remember that since we only store odd numbered
    // indexes in each row, this may actually be one position to the left of max_left_position
    // in the case that is even.  Then, if we only evaluate f(-x_i) for abscissa values 
    // i <= max_left_index we will never evaluate f(-x_i) at the left endpoint.
    // max_right_position and max_right_index are defained similarly for the right boundary
    // and are used to guard evaluation of f(x_i).
    //
    // max_left_position and max_right_position start off as the last element in row zero:
    // 
    std::size_t max_left_position(m_abscissas[0].size() - 1);
    std::size_t max_left_index, max_right_position(max_left_position), max_right_index;
    //
    // Decrement max_left_position and max_right_position until the complement 
    // of the abscissa value is greater than the smallest permitted (as specified
    // by the function caller):
    //
    while (max_left_position && fabs(m_abscissas[0][max_left_position]) < left_min_complement)
       --max_left_position;
    while (max_right_position && fabs(m_abscissas[0][max_right_position]) < right_min_complement)
       --max_right_position;
    //
    // Assumption: left_min_complement/right_min_complement are sufficiently small that we only
    // ever decrement through the stored values that are complements (the negative ones), and 
    // never ever hit the true abscissa values (positive stored values).
    //
    BOOST_ASSERT(m_abscissas[0][max_left_position] < 0);
    BOOST_ASSERT(m_abscissas[0][max_right_position] < 0);


    Real h = m_t_max / m_inital_row_length;
    Real I0 = half_pi<Real>()*f(0, 1);
    Real L1_I0 = abs(I0);
    for(size_t i = 1; i < m_abscissas[0].size(); ++i)
    {
        if ((i > max_right_position) && (i > max_left_position))
            break;
        Real x = m_abscissas[0][i];
        Real xc = x;
        Real w = m_weights[0][i];
        if ((boost::math::signbit)(x))
        {
           // We have stored x - 1:
           x = 1 + xc;
        }
        else
           xc = x - 1;
        Real yp, ym;
        yp = i <= max_right_position ? f(x, -xc) : 0;
        ym = i <= max_left_position ? f(-x, xc) : 0;
        I0 += (yp + ym)*w;
        L1_I0 += (abs(yp) + abs(ym))*w;
    }
    //
    // We have:
    // k = current row.
    // I0 = last integral value.
    // I1 = current integral value.
    // L1_I0 and L1_I1 are the absolute integral values.
    //
    size_t k = 1;
    Real I1 = I0;
    Real L1_I1 = L1_I0;
    Real err = 0;

    while (k < 4 || (k < m_weights.size() && k < m_max_refinements) )
    {
        I0 = I1;
        L1_I0 = L1_I1;

        I1 = half<Real>()*I0;
        L1_I1 = half<Real>()*L1_I0;
        h *= half<Real>();
        Real sum = 0;
        Real absum = 0;
        auto const& abscissa_row = this->get_abscissa_row(k);
        auto const& weight_row = this->get_weight_row(k);
        std::size_t first_complement_index = this->get_first_complement_index(k);
        //
        // At the start of each new row we need to update the max left/right indexes
        // at which we can evaluate f(x_i).  The new logical position is simply twice
        // the old value.  The new max index is one position to the left of the new
        // logical value (remmember each row contains only odd numbered positions).
        // Then we have to make a single check, to see if one position to the right
        // is also in bounds (this is the new abscissa value in this row which is 
        // known to be in between a value known to be in bounds, and one known to be
        // not in bounds).
        // Thus, we filter which abscissa values generate a call to f(x_i), with a single
        // floating point comparison per loop.  Everything else is integer logic.
        //
        max_left_index = max_left_position - 1;
        max_left_position *= 2;
        max_right_index = max_right_position - 1;
        max_right_position *= 2;
        if ((abscissa_row.size() > max_left_index + 1) && (fabs(abscissa_row[max_left_index + 1]) > left_min_complement))
        {
           ++max_left_position;
           ++max_left_index;
        }
        if ((abscissa_row.size() > max_right_index + 1) && (fabs(abscissa_row[max_right_index + 1]) > right_min_complement))
        {
           ++max_right_position;
           ++max_right_index;
        }

        for(size_t j = 0; j < weight_row.size(); ++j)
        {
            // If both left and right abscissa values are out of bounds at this step
            // we can just stop this loop right now:
            if ((j > max_left_index) && (j > max_right_index))
                break;
            Real x = abscissa_row[j];
            Real xc = x;
            Real w = weight_row[j];
            if (j >= first_complement_index)
            {
               // We have stored x - 1:
               BOOST_ASSERT(x < 0);
               x = 1 + xc;
            }
            else
            {
               BOOST_ASSERT(x >= 0);
               xc = x - 1;
            }

            Real yp = j > max_right_index ? 0 : f(x, -xc);
            Real ym = j > max_left_index ? 0 : f(-x, xc);
            Real term = (yp + ym)*w;
            sum += term;

            // A question arises as to how accurately we actually need to estimate the L1 integral.
            // For simple integrands, computing the L1 norm makes the integration 20% slower,
            // but for more complicated integrands, this calculation is not noticeable.
            Real abterm = (abs(yp) + abs(ym))*w;
            absum += abterm;
        }

        I1 += sum*h;
        L1_I1 += absum*h;
        ++k;
        err = abs(I0 - I1);
        // std::cout << "Estimate:        " << I1 << " Error estimate at level " << k  << " = " << err << std::endl;

        if (!(boost::math::isfinite)(I1))
        {
            return policies::raise_evaluation_error(function, "The tanh_sinh quadrature evaluated your function at a singular point at got %1%. Please narrow the bounds of integration or check your function for singularities.", I1, Policy());
        }
        //
        // Termination condition:
        // No more levels are considered once the error is less than the specified tolerance.
        // Note however, that we always go down at least 4 levels, otherwise we risk missing
        // features of interest in f() - imagine for example a function which flatlines, except
        // for a very small "spike". An example would be the incomplete beta integral with large
        // parameters.  We could keep hunting until we find something, but that would handicap
        // integrals which really are zero.... so a compromise then!
        //
        if ((k > 4) && (err <= m_tol*L1_I1))
        {
            break;
        }

    }
    if (error)
    {
        *error = err;
    }

    if (L1)
    {
        *L1 = L1_I1;
    }

    return I1;
}

template<class Real, class Policy>
void tanh_sinh_detail<Real, Policy>::init(const mpl::int_<0>&)
{
   using std::tanh;
   using std::sinh;
   using std::asinh;
   using std::atanh;
   using boost::math::constants::half_pi;
   using boost::math::constants::pi;
   using boost::math::constants::two_div_pi;

   m_inital_row_length = 7;
   std::size_t first_complement = 0;
   m_t_max = m_inital_row_length;
   m_t_crossover = t_from_abscissa_complement(Real(0.5f));

   m_abscissas.assign(m_max_refinements + 1, std::vector<Real>());
   m_weights.assign(m_max_refinements + 1, std::vector<Real>());
   m_first_complements.assign(m_max_refinements + 1, 0);
   //
   // First row is special:
   //
   Real h = m_t_max / m_inital_row_length;

   std::vector<Real> temp(m_inital_row_length + 1, Real(0));
   for (std::size_t i = 0; i < m_inital_row_length; ++i)
   {
      Real t = h * i;
      if ((first_complement < 0) && (t < m_t_crossover))
         first_complement = i;
      temp[i] = t < m_t_crossover ? abscissa_at_t(t) : -abscissa_complement_at_t(t);
   }
   temp[m_inital_row_length] = abscissa_complement_at_t(m_t_max);
   m_abscissas[0].swap(temp);
   m_first_complements[0] = first_complement;
   temp.assign(m_inital_row_length + 1, Real(0));
   for (std::size_t i = 0; i < m_inital_row_length; ++i)
      temp[i] = weight_at_t(Real(h * i));
   temp[m_inital_row_length] = weight_at_t(m_t_max);
   m_weights[0].swap(temp);

   for (std::size_t row = 1; row <= m_committed_refinements; ++row)
   {
      h /= 2;
      first_complement = 0;

      for (Real pos = h; pos < m_t_max; pos += 2 * h)
      {
         if (pos < m_t_crossover)
            ++first_complement;
         temp.push_back(pos < m_t_crossover ? abscissa_at_t(pos) : -abscissa_complement_at_t(pos));
      }
      m_abscissas[row].swap(temp);
      m_first_complements[row] = first_complement;
      for (Real pos = h; pos < m_t_max; pos += 2 * h)
         temp.push_back(weight_at_t(pos));
      m_weights[row].swap(temp);
   }
}

template<class Real, class Policy>
void tanh_sinh_detail<Real, Policy>::init(const mpl::int_<1>&)
{
   m_inital_row_length = 4;
   m_abscissas.reserve(m_max_refinements + 1);
   m_weights.reserve(m_max_refinements + 1);
   m_first_complements.reserve(m_max_refinements + 1);
   m_abscissas = {
      { 0.0f, -0.04863203593f, -2.252280754e-05f, -4.294161056e-14f, -1.167648898e-37f, },
      { -0.3257285078f, -0.002485143543f, -1.112433512e-08f, -5.378491591e-23f, },
      { 0.3772097382f, -0.1404309413f, -0.01295943949f, -0.0003117359716f, -7.952628853e-07f, -4.714355182e-11f, -5.415222824e-18f, -2.040300394e-29f, },
      { 0.1943570033f, -0.4608532946f, -0.219392561f, -0.08512073674f, -0.0260331318f, -0.005944493369f, -0.0009348035442f, -9.061530486e-05f, -4.683958779e-06f, -1.072183876e-07f, -8.572949078e-10f, -1.767834693e-12f, -6.374878495e-16f, -2.442937279e-20f, -5.251546473e-26f, -2.789467162e-33f, },
      { 0.09792388529f, 0.2878799327f, 0.4612535439f, -0.3897263425f, -0.2689819652f, -0.1766829945f, -0.1101085972f, -0.06483914248f, -0.03588783578f, -0.01854517332f, -0.008873007558f, -0.003891334562f, -0.001545791232f, -0.0005485655647f, -0.0001711779271f, -4.612899437e-05f, -1.051798518e-05f, -1.982859405e-06f, -3.011058474e-07f, -3.576091908e-08f, -3.212800902e-09f, -2.102671378e-10f, -9.606066479e-12f, -2.919026641e-13f, -5.586116639e-15f, -6.328207135e-17f, -3.956461339e-19f, -1.260975845e-21f, -1.872492175e-24f, -1.170030294e-27f, -2.740986178e-31f, -2.112282721e-35f, },
      { 0.04905596731f, 0.1464179843f, 0.2415663195f, 0.3331422646f, 0.4199521113f, -0.4989866106f, -0.4244155094f, -0.356823241f, -0.2964499949f, -0.2433060914f, -0.1972012587f, -0.1577807536f, -0.1245646024f, -0.09698671849f, -0.07443136593f, -0.05626521395f, -0.04186397729f, -0.0306332671f, -0.02202376481f, -0.01554116883f, -0.01075156891f, -0.007283002803f, -0.004823973845f, -0.003119681872f, -0.001966663685f, -0.001206465701f, -0.0007188880782f, -0.0004152496485f, -0.0002320284004f, -0.0001251349512f, -6.498007492e-05f, -3.240693206e-05f, -1.548009773e-05f, -7.062123337e-06f, -3.06755081e-06f, -1.264528134e-06f, -4.929942806e-07f, -1.811062872e-07f, -6.244592163e-08f, -2.01254968e-08f, -6.035865798e-09f, -1.676638052e-09f, -4.292122274e-10f, -1.007222767e-10f, -2.154466259e-11f, -4.175393124e-12f, -7.284737264e-13f, -1.136386985e-13f, -1.57355351e-14f, -1.91921891e-15f, -2.044959541e-16f, -1.886958307e-17f, -1.493871649e-18f, -1.00469381e-19f, -5.679929178e-21f, -2.669103953e-22f, -1.030178138e-23f, -3.224484876e-25f, -8.0747829e-27f, -1.594654602e-28f, -2.445723975e-30f, -2.865919772e-32f, -2.521682525e-34f, -1.63551444e-36f, },
      { 0.02453976357f, 0.07352512299f, 0.1222291222f, 0.1704679724f, 0.2180634735f, 0.2648450766f, 0.3106517806f, 0.3553338252f, 0.3987541505f, 0.440789599f, 0.4813318461f, -0.4797119493f, -0.4424187717f, -0.4068496464f, -0.3730497919f, -0.3410490083f, -0.3108622749f, -0.2824905325f, -0.2559216165f, -0.2311313132f, -0.2080845076f, -0.1867363915f, -0.1670337061f, -0.148915992f, -0.1323168242f, -0.1171650118f, -0.1033857457f, -0.09090168184f, -0.07963394697f, -0.069503062f, -0.06042977607f, -0.05233580938f, -0.04514450419f, -0.03878138485f, -0.03317462969f, -0.02825545843f, -0.02395843974f, -0.0202217242f, -0.01698720852f, -0.01420063697f, -0.0118116462f, -0.009773759532f, -0.008044336997f, -0.006584486831f, -0.005358944287f, -0.004335923183f, -0.00348694536f, -0.002786652957f, -0.002212608041f, -0.001745083828f, -0.001366851359f, -0.001062965166f, -0.0008205510651f, -0.0006285988591f, -0.0004777623488f, -0.0003601686544f, -0.0002692384802f, -0.0001995185689f, -0.0001465272269f, -0.0001066134524f, -7.682987071e-05f, -5.481938554e-05f, -3.871519214e-05f, -2.705357477e-05f, -1.869872988e-05f, -1.2778718e-05f, -8.631551655e-06f, -5.760372383e-06f, -3.796652834e-06f, -2.470376195e-06f, -1.586189035e-06f, -1.00458931e-06f, -6.272926646e-07f, -3.860114498e-07f, -2.339766676e-07f, -1.396287854e-07f, -8.199520529e-08f, -4.735733554e-08f, -2.688676406e-08f, -1.499692369e-08f, -8.213543901e-09f, -4.414366384e-09f, -2.326763262e-09f, -1.2020165e-09f, -6.082231242e-10f, -3.012456307e-10f, -1.459438845e-10f, -6.911160499e-11f, -3.196678326e-11f, -1.443120992e-11f, -6.353676128e-12f, -2.725950519e-12f, -1.138734572e-12f, -4.627734622e-13f, -1.827990225e-13f, -7.012046738e-14f, -2.609605366e-14f, -9.413361335e-15f, -3.287925695e-15f, -1.11086294e-15f, -3.626602264e-16f, -1.142787653e-16f, -3.471902269e-17f, -1.015781907e-17f, -2.858532825e-18f, -7.727834787e-19f, -2.004423992e-19f, -4.981568376e-20f, -1.184668492e-20f, -2.691984904e-21f, -5.836667442e-22f, -1.205661589e-22f, -2.369109351e-23f, -4.421339462e-24f, -7.823835004e-25f, -1.310533144e-25f, -2.074345013e-26f, -3.096968326e-27f, -4.353200528e-28f, -5.749955668e-29f, -7.122715927e-30f, -8.25783542e-31f, -8.941541007e-32f, -9.02279665e-33f, -8.46603012e-34f, -7.369272807e-35f, -5.936632356e-36f, -4.415277839e-37f, },
      { 0.01227135512f, 0.03680228095f, 0.06129788941f, 0.08573475488f, 0.1100896299f, 0.1343395153f, 0.1584617283f, 0.1824339697f, 0.2062343883f, 0.2298416433f, 0.2532349634f, 0.2763942036f, 0.2992998981f, 0.3219333097f, 0.3442764756f, 0.3663122492f, 0.3880243378f, 0.4093973357f, 0.4304167537f, 0.4510690435f, 0.4713416183f, 0.4912228687f, -0.489297826f, -0.4702300899f, -0.4515825479f, -0.4333628262f, -0.4155775577f, -0.39823239f, -0.3813319982f, -0.3648801001f, -0.3488794756f, -0.3333319877f, -0.318238608f, -0.3035994429f, -0.2894137636f, -0.275680037f, -0.2623959593f, -0.2495584902f, -0.2371638893f, -0.2252077531f, -0.2136850525f, -0.2025901721f, -0.1919169483f, -0.1816587092f, -0.1718083133f, -0.1623581887f, -0.1533003716f, -0.1446265448f, -0.1363280753f, -0.1283960505f, -0.120821315f, -0.113594505f, -0.1067060822f, -0.1001463668f, -0.09390556883f, -0.08797381831f, -0.08234119416f, -0.07699775172f, -0.07193354881f, -0.06713867034f, -0.0626032516f, -0.0583175f, -0.0542717154f, -0.050456309f, -0.0468618209f, -0.0434789361f, -0.0402984993f, -0.03731152826f, -0.0345092259f, -0.03188299107f, -0.02942442821f, -0.02712535566f, -0.02497781299f, -0.02297406711f, -0.02110661737f, -0.01936819969f, -0.01775178968f, -0.01625060483f, -0.01485810598f, -0.01356799776f, -0.01237422849f, -0.01127098916f, -0.01025271192f, -0.009314067826f, -0.008449964034f, -0.007655540506f, -0.006926166179f, -0.006257434709f, -0.005645159804f, -0.005085370197f, -0.004574304294f, -0.004108404533f, -0.003684311494f, -0.003298857801f, -0.002949061834f, -0.002632121306f, -0.002345406714f, -0.002086454715f, -0.001852961444f, -0.001642775797f, -0.001453892723f, -0.001284446528f, -0.001132704236f, -0.0009970590063f, -0.0008760236476f, -0.0007682242321f, -0.0006723938372f, -0.000587366425f, -0.0005120708762f, -0.0004455251889f, -0.0003868308567f, -0.0003351674323f, -0.0002897872882f, -0.0002500105784f, -0.0002152204083f, -0.0001848582177f, -0.0001584193765f, -0.0001354489984f, -0.0001155379702f, -9.8319198e-05f, -8.346406605e-05f, -7.067910812e-05f, -5.970288552e-05f, -5.030306831e-05f, -4.227371392e-05f, -3.543273737e-05f, -2.961956641e-05f, -2.469297451e-05f, -2.052908425e-05f, -1.701953324e-05f, -1.406979453e-05f, -1.159764317e-05f, -9.531760786e-06f, -7.810469566e-06f, -6.380587461e-06f, -5.196396351e-06f, -4.218715072e-06f, -3.414069429e-06f, -2.753951574e-06f, -2.214161365e-06f, -1.774222689e-06f, -1.416868016e-06f, -1.127584838e-06f, -8.942180042e-07f, -7.066223315e-07f, -5.563602711e-07f, -4.364397681e-07f, -3.410878407e-07f, -2.65555767e-07f, -2.059521239e-07f, -1.591002641e-07f, -1.224171449e-07f, -9.381073151e-08f, -7.159348586e-08f, -5.440972705e-08f, -4.117489851e-08f, -3.102500976e-08f, -2.327473287e-08f, -1.738282654e-08f, -1.292373523e-08f, -9.564367214e-09f, -7.045195508e-09f, -5.164949276e-09f, -3.768272997e-09f, -2.735826254e-09f, -1.976380568e-09f, -1.420541964e-09f, -1.015790099e-09f, -7.225780068e-10f, -5.112816947e-10f, -3.598270551e-10f, -2.518536224e-10f, -1.753014775e-10f, -1.213298105e-10f, -8.349395075e-11f, -5.712266027e-11f, -3.8849686e-11f, -2.626342738e-11f, -1.764649978e-11f, -1.178329832e-11f, -7.818680628e-12f, -5.154836404e-12f, -3.376501461e-12f, -2.19707447e-12f, -1.420047367e-12f, -9.115800793e-13f, -5.811306251e-13f, -3.67867911e-13f, -2.312068904e-13f, -1.442617249e-13f, -8.934966665e-14f, -5.492567772e-14f, -3.35078831e-14f, -2.02840764e-14f, -1.218279513e-14f, -7.258853736e-15f, -4.290062787e-15f, -2.514652539e-15f, -1.461687113e-15f, -8.424330958e-16f, -4.81351759e-16f, -2.726317943e-16f, -1.530442297e-16f, -8.513785725e-17f, -4.692795039e-17f, -2.562597595e-17f, -1.386133467e-17f, -7.425750192e-18f, -3.939309402e-18f, -2.069079146e-18f, -1.075828622e-18f, -5.536671017e-19f, -2.819834004e-19f, -1.421006375e-19f, -7.084243878e-20f, -3.493350557e-20f, -1.703597751e-20f, -8.214706044e-21f, -3.915973438e-21f, -1.845149816e-21f, -8.591874581e-22f, -3.953006958e-22f, -1.7966698e-22f, -8.065408821e-23f, -3.575337212e-23f, -1.564782132e-23f, -6.760041764e-24f, -2.882136323e-24f, -1.212436233e-24f, -5.031421831e-25f, -2.059286312e-25f, -8.310787798e-26f, -3.306518068e-26f, -1.296597346e-26f, -5.010091032e-27f, -1.907179385e-27f, -7.150567334e-28f, -2.639906037e-28f, -9.594664542e-29f, -3.432078099e-29f, -1.207985066e-29f, -4.182464721e-30f, -1.424153707e-30f, -4.767833382e-31f, -1.568949178e-31f, -5.073438855e-32f, -1.611691916e-32f, -5.028356694e-33f, -1.540320437e-33f, -4.631392443e-34f, -1.366470225e-34f, -3.955008527e-35f, -1.122591825e-35f, -3.123848743e-36f, -8.519540247e-37f, -2.276473481e-37f, },
   };
   m_weights = {
      { 1.570796327f, 0.2300223945f, 0.0002662005138f, 1.358178427e-12f, 1.001741678e-35f, },
      { 0.9659765794f, 0.01834316699f, 2.143120456e-07f, 2.800315102e-21f, },
      { 1.389614759f, 0.5310782754f, 0.07638574357f, 0.002902517748f, 1.198370136e-05f, 1.163116581e-09f, 2.197079236e-16f, 1.363510331e-27f, },
      { 1.523283719f, 1.193463026f, 0.7374378484f, 0.3604614185f, 0.1374221077f, 0.03917500549f, 0.007742601026f, 0.0009499468043f, 6.248255924e-05f, 1.826332059e-06f, 1.868728227e-08f, 4.937853878e-11f, 2.28349267e-14f, 1.122753143e-18f, 3.09765397e-24f, 2.112123344e-31f, },
      { 1.558773356f, 1.466014427f, 1.29747575f, 1.081634985f, 0.8501728565f, 0.6304051352f, 0.4408332363f, 0.2902406793f, 0.1793244121f, 0.1034321542f, 0.05528968374f, 0.02713351001f, 0.0120835436f, 0.004816298144f, 0.001690873998f, 0.0005133938241f, 0.0001320523413f, 2.811016433e-05f, 4.823718203e-06f, 6.477756604e-07f, 6.583518513e-08f, 4.876006097e-09f, 2.521634792e-10f, 8.675931415e-12f, 1.880207173e-13f, 2.412423038e-15f, 1.708453277e-17f, 6.168256849e-20f, 1.037679724e-22f, 7.345984103e-26f, 1.949783362e-29f, 1.702438776e-33f, },
      { 1.567781431f, 1.543881116f, 1.497226223f, 1.430008355f, 1.345278885f, 1.246701207f, 1.138272243f, 1.024044933f, 0.9078793792f, 0.7932427008f, 0.6830685163f, 0.5796781031f, 0.4847580912f, 0.3993847415f, 0.3240825396f, 0.2589046395f, 0.2035239989f, 0.1573262035f, 0.1194974113f, 0.08910313924f, 0.06515553343f, 0.04666820805f, 0.03269873273f, 0.02237947106f, 0.0149378351f, 0.009707223739f, 0.006130037632f, 0.003754250977f, 0.002225082706f, 0.001273327945f, 0.0007018595157f, 0.0003716669362f, 0.0001885644298f, 9.139081749e-05f, 4.218318384e-05f, 1.84818136e-05f, 7.659575853e-06f, 2.991661588e-06f, 1.096883513e-06f, 3.759541186e-07f, 1.199244278e-07f, 3.543477717e-08f, 9.649888896e-09f, 2.409177326e-09f, 5.48283578e-10f, 1.130605535e-10f, 2.09893354e-11f, 3.484193767e-12f, 5.134127525e-13f, 6.663992283e-14f, 7.556721776e-15f, 7.420993231e-16f, 6.252804845e-17f, 4.475759507e-18f, 2.693120661e-19f, 1.346994157e-20f, 5.533583499e-22f, 1.843546975e-23f, 4.913936871e-25f, 1.032939131e-26f, 1.686277004e-28f, 2.103305749e-30f, 1.96992098e-32f, 1.359989462e-34f, },
      { 1.570042029f, 1.564021404f, 1.55205317f, 1.534281738f, 1.510919723f, 1.482243298f, 1.448586255f, 1.410332971f, 1.367910512f, 1.321780117f, 1.272428346f, 1.22035811f, 1.16607987f, 1.110103194f, 1.05292888f, 0.995041804f, 0.9369046127f, 0.8789523456f, 0.8215880353f, 0.7651792989f, 0.7100559012f, 0.6565082461f, 0.6047867306f, 0.555101878f, 0.5076251588f, 0.4624903981f, 0.4197956684f, 0.3796055694f, 0.3419537959f, 0.3068459094f, 0.2742622297f, 0.2441607779f, 0.2164802091f, 0.1911426841f, 0.1680566379f, 0.1471194133f, 0.1282197336f, 0.111239999f, 0.09605839187f, 0.08255078811f, 0.07059246991f, 0.06005964236f, 0.05083075757f, 0.04278765216f, 0.0358165056f, 0.02980862812f, 0.02466108731f, 0.02027718382f, 0.01656678625f, 0.01344653661f, 0.01083993717f, 0.00867733075f, 0.006895785969f, 0.005438899798f, 0.004256529599f, 0.003304466994f, 0.002544065768f, 0.001941835776f, 0.00146901436f, 0.001101126113f, 0.0008175410133f, 0.0006010398799f, 0.0004373949562f, 0.0003149720919f, 0.0002243596521f, 0.000158027884f, 0.0001100211285f, 7.568399659e-05f, 5.142149745e-05f, 3.449212476e-05f, 2.283211811e-05f, 1.490851403e-05f, 9.598194128e-06f, 6.089910032e-06f, 3.806198326e-06f, 2.342166721e-06f, 1.418306716e-06f, 8.447375638e-07f, 4.94582887e-07f, 2.844992366e-07f, 1.606939458e-07f, 8.907139514e-08f, 4.84209502e-08f, 2.579956823e-08f, 1.346464552e-08f, 6.878461096e-09f, 3.437185674e-09f, 1.678889768e-09f, 8.009978448e-10f, 3.729950184e-10f, 1.693945779e-10f, 7.496739757e-11f, 3.230446433e-11f, 1.354251291e-11f, 5.518236947e-12f, 2.18359221e-12f, 8.383128961e-13f, 3.119497729e-13f, 1.124020896e-13f, 3.917679451e-14f, 1.319434223e-14f, 4.289196222e-15f, 1.344322288e-15f, 4.057557702e-16f, 1.177981213e-16f, 3.285386163e-17f, 8.791316559e-18f, 2.25407483e-18f, 5.530176913e-19f, 1.296452714e-19f, 2.899964556e-20f, 6.180143249e-21f, 1.252867643e-21f, 2.412250547e-22f, 4.4039067e-23f, 7.610577808e-24f, 1.242805165e-24f, 1.91431069e-25f, 2.776125103e-26f, 3.783124073e-27f, 4.834910155e-28f, 5.783178697e-29f, 6.460575703e-30f, 6.72603739e-31f, 6.511153451e-32f, 5.847409075e-33f, 4.860046055e-34f, 3.72923953e-35f, },
      { 1.570607717f, 1.569099695f, 1.566088239f, 1.561582493f, 1.555596115f, 1.548147191f, 1.539258145f, 1.528955608f, 1.517270275f, 1.504236738f, 1.489893298f, 1.474281762f, 1.457447221f, 1.439437815f, 1.420304486f, 1.400100716f, 1.378882264f, 1.35670689f, 1.333634075f, 1.309724744f, 1.285040985f, 1.259645765f, 1.233602657f, 1.206975567f, 1.179828472f, 1.152225159f, 1.124228984f, 1.09590263f, 1.067307886f, 1.038505436f, 1.00955466f, 0.9805134517f, 0.951438051f, 0.9223828892f, 0.8934004523f, 0.8645411596f, 0.8358532563f, 0.807382723f, 0.7791731997f, 0.7512659245f, 0.723699687f, 0.6965107951f, 0.6697330554f, 0.6433977657f, 0.6175337199f, 0.5921672237f, 0.5673221206f, 0.5430198278f, 0.5192793805f, 0.4961174844f, 0.4735485755f, 0.4515848861f, 0.4302365164f, 0.4095115109f, 0.3894159397f, 0.3699539819f, 0.3511280132f, 0.3329386948f, 0.3153850641f, 0.2984646265f, 0.2821734476f, 0.2665062456f, 0.2514564831f, 0.2370164583f, 0.2231773949f, 0.2099295305f, 0.1972622032f, 0.1851639366f, 0.1736225217f, 0.1626250975f, 0.1521582278f, 0.1422079761f, 0.1327599774f, 0.1237995069f, 0.1153115463f, 0.1072808458f, 0.09969198461f, 0.09252942711f, 0.08577757654f, 0.0794208254f, 0.07344360286f, 0.06783041903f, 0.06256590638f, 0.05763485811f, 0.05302226366f, 0.04871334138f, 0.04469356846f, 0.04094870813f, 0.0374648342f, 0.03422835312f, 0.03122602351f, 0.02844497325f, 0.02587271434f, 0.02349715546f, 0.02130661237f, 0.01928981624f, 0.01743592007f, 0.01573450311f, 0.01417557353f, 0.01274956936f, 0.01144735783f, 0.01026023317f, 0.009179912924f, 0.008198533005f, 0.007308641451f, 0.006503191044f, 0.005775530877f, 0.005119396961f, 0.004528901979f, 0.003998524263f, 0.00352309611f, 0.003097791523f, 0.002718113458f, 0.002379880688f, 0.002079214354f, 0.001812524299f, 0.001576495262f, 0.00136807301f, 0.001184450486f, 0.001023054043f, 0.0008815298242f, 0.0007577303578f, 0.0006497014187f, 0.0005556692074f, 0.000474027894f, 0.0004033275645f, 0.0003422626065f, 0.0002896605611f, 0.0002444714673f, 0.0002057577147f, 0.0001726844199f, 0.0001445103343f, 0.0001205792873f, 0.0001003121646f, 8.319941724e-05f, 6.879409311e-05f, 5.670537985e-05f, 4.659264463e-05f, 3.815995412e-05f, 3.115105568e-05f, 2.534479897e-05f, 2.055097594e-05f, 1.660655598e-05f, 1.337229228e-05f, 1.072967541e-05f, 8.578209354e-06f, 6.832986277e-06f, 5.422535892e-06f, 4.286926494e-06f, 3.376095235e-06f, 2.648386225e-06f, 2.069276126e-06f, 1.610268009e-06f, 1.247935512e-06f, 9.631005212e-07f, 7.401289349e-07f, 5.66330284e-07f, 4.314482559e-07f, 3.272303733e-07f, 2.470662451e-07f, 1.856849137e-07f, 1.3890287e-07f, 1.034152804e-07f, 7.662387397e-08f, 5.649576387e-08f, 4.144823356e-08f, 3.025519646e-08f, 2.197164892e-08f, 1.587297809e-08f, 1.140646555e-08f, 8.152746483e-09f, 5.795349573e-09f, 4.096757914e-09f, 2.879701346e-09f, 2.012621022e-09f, 1.398441431e-09f, 9.659485186e-10f, 6.632086347e-10f, 4.52575761e-10f, 3.069270208e-10f, 2.068420354e-10f, 1.385028753e-10f, 9.214056423e-11f, 6.089338706e-11f, 3.997338952e-11f, 2.60619605e-11f, 1.687451934e-11f, 1.084916183e-11f, 6.925528015e-12f, 4.38886519e-12f, 2.760858767e-12f, 1.723764404e-12f, 1.068075044e-12f, 6.56694435e-13f, 4.00598538e-13f, 2.424296605e-13f, 1.455249916e-13f, 8.663812725e-14f, 5.114974901e-14f, 2.99421776e-14f, 1.737681695e-14f, 9.99642401e-15f, 5.699626666e-15f, 3.220432513e-15f, 1.802958964e-15f, 9.999957344e-16f, 5.493978397e-16f, 2.989420886e-16f, 1.610765424e-16f, 8.593209748e-17f, 4.538246827e-17f, 2.372253167e-17f, 1.227167167e-17f, 6.281229049e-18f, 3.180614714e-18f, 1.593049257e-18f, 7.890855159e-19f, 3.864733103e-19f, 1.87127733e-19f, 8.955739455e-20f, 4.235742852e-20f, 1.979436202e-20f, 9.138078558e-21f, 4.166641158e-21f, 1.876075055e-21f, 8.339901949e-22f, 3.659575236e-22f, 1.584785218e-22f, 6.771575694e-23f, 2.854281708e-23f, 1.186583858e-23f, 4.864069936e-24f, 1.965643419e-24f, 7.829165625e-25f, 3.072789229e-25f, 1.188107615e-25f, 4.524619749e-26f, 1.696710187e-26f, 6.263641003e-27f, 2.275790793e-27f, 8.136077716e-28f, 2.861306549e-28f, 9.896184197e-29f, 3.365200893e-29f, 1.124807055e-29f, 3.694460433e-30f, 1.192093301e-30f, 3.777757876e-31f, 1.175436379e-31f, 3.589879078e-32f, 1.075842686e-32f, 3.162835126e-33f, 9.118674189e-34f, 2.577393168e-34f, 7.139829504e-35f, 1.937828921e-35f, },
   };
   m_first_complements = {
      1, 0, 1, 1, 3, 5, 11, 22,
   };

   while (m_abscissas.size() <= m_committed_refinements)
      extend_refinements();
   m_committed_refinements = m_abscissas.size() - 1;

   if (m_max_refinements >= m_abscissas.size())
   {
      m_abscissas.resize(m_max_refinements + 1);
      m_weights.resize(m_max_refinements + 1);
      m_first_complements.resize(m_max_refinements + 1);
   }
   else
   {
      m_max_refinements = m_abscissas.size() - 1;
   }
   m_t_max = m_inital_row_length;
   m_t_crossover = t_from_abscissa_complement(Real(0.5f));
}


}}}}  // namespaces
#endif
