#include <iostream>
#include <cmath>
#include <random>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

using namespace std;
namespace py = pybind11;

// parameters to be inferred

// Maximum sodium concuctance
double gbar_Na; // [mS/cm2]
// Maximum potassium concuctance
double gbar_K; // [mS/cm2]
// Maximum conductance for adaptive potassium currents (these can induce firing rate adaptations)
double gbar_M; // [mS/cm2]
// Leak conductance
double g_leak; // [mS/cm2]
// time constant of the adaptive potassium current (gives an idea of when these channels will turn active)
double tau_max; // [ms]
// Vt: a 'threshold' voltage that can influence the dynamics of all channels
double Vt; // [mV]
// Leak potential
double   E_leak; // [mV]
// A factor that can change the amount of injected current, or equivalently change the compartment area initially
// deduced if it sees fit to do so
double I_scale;
// A factor that can make the dynamics of Na+ and K+ channels faster/slower. This was included to find simulations
// that can recover the shape of the 1st action potential fired.
double rate_to_SS_factor;

// kinetics
double efun(double z){
  double out = 0;
  if(abs(z) < 0.0001){
    out = 1 - z / 2;
  }
  else{
    out = z / (exp(z) - 1);
  }
  return out;
}

double alpha_m(double x){
  double v1 = x - Vt - 13.0;
  return 0.32 * efun(-0.25 * v1) / 0.25;
}

double beta_m(double x){
  double v1 = x - Vt - 40.0;
  return 0.28 * efun(0.2 * v1) / 0.2;
}

double alpha_h(double x){
  double v1 = x - Vt - 17.0;
  return 0.128 * exp(-v1 / 18.0);
}

double beta_h(double x){
  double v1 = x - Vt - 40.0;
  return 4.0 / (1 + exp(-0.2 * v1));
}

double alpha_n(double x){
  double v1 = x - Vt - 15;
  return 0.032 * efun(-0.2 * v1) / 0.2;
}

double beta_n(double x){
  double v1 = x - Vt - 10.0;
  return 0.5 * exp(-v1 / 40);
}

// steady-states and time constants
double tau_n(double x){
  return rate_to_SS_factor*1.0 / (alpha_n(x) + beta_n(x));
}

double n_inf(double x){
  return alpha_n(x) / (alpha_n(x) + beta_n(x));
}

double tau_m(double x){
  return rate_to_SS_factor*1.0 / (alpha_m(x) + beta_m(x));
}

double m_inf(double x){
  return alpha_m(x) / (alpha_m(x) + beta_m(x));
}

double tau_h(double x){
  return rate_to_SS_factor*1.0 / (alpha_h(x) + beta_h(x));
}

double h_inf(double x){
  return alpha_h(x) / (alpha_h(x) + beta_h(x));
}

// slow non-inactivating K+
double p_inf(double x){
  double v1 = x + 35.0;
  return 1.0 / (1.0 + exp(-0.1 * v1));
}

double tau_p(double x){
  double v1 = x + 35.0;
  return tau_max / (3.3 * exp(0.05 * v1) + exp(-0.05 * v1));
}


double * runHH(double * parameters, int random_seed, double V0, double * I, double dt, double tfin){
  double t_start = 0.0;
  double t_dur = tfin - t_start;
  int size = int(t_dur/dt);

  // experimental parameters
  double * V = (double *)malloc(sizeof(double)*size);
  double n[size];
  double m[size];
  double h[size];
  double p[size];

  V[0] = V0;
  n[0] = n_inf(V[0]);
  m[0] = m_inf(V[0]);
  h[0] = h_inf(V[0]);
  p[0] = p_inf(V[0]);

  gbar_Na = parameters[0];
  gbar_K = parameters[1];
  gbar_M = parameters[2];
  g_leak = parameters[3];
  tau_max = parameters[4]; // [ms]
  Vt = parameters[5]; // [mV]
  E_leak = parameters[6]; // [mV]
  // I_scale = parameters[7];
  rate_to_SS_factor = parameters[7];

  // fixed parameters (g_leak, C from fitting hyperpolarization trace)
  // g_leak = 0.117  # mS/cm2
  // Vt = -60.0  # mV
  // E_leak = np.mean(voltage_obs[0:2500, curr_index])  # mV
  double nois_fact = 0.1;  // [uA/cm2]
  double C = 1;  // uF/cm2
  double E_Na = 53.0;  // mV            # TODO: check with Federico
  double E_K = -90.0;  // mV            # TODO: check with Federico
  double Q10 = 3.0;
  double T_1 = 36.0;                           // °C, from paper Martin Pospischil et al.
  double T_2 = 34.0;                           // °C, experiment was actually done at 34 °C
  double T_adj_factor = pow(Q10,((T_2-T_1)/10)); // temperature coeff., https://en.wikipedia.org/wiki/Q10_(temperature_coefficient)


 // RandomState
 default_random_engine generator(random_seed);
 normal_distribution<double> distribution(0.0, 1.0);

// --------------------------------------------
// simulation from initial point
  for(int i = 1; i < size; i++){
    double tau_V_inv = (
            (pow(m[i - 1], 3)) * gbar_Na * h[i - 1]
            + (pow(n[i - 1],4)) * gbar_K
            + g_leak
            + gbar_M * p[i - 1]
        ) / C;
        double V_inf = (
            (pow(m[i - 1],3)) * gbar_Na * h[i - 1] * E_Na
            + (pow(n[i - 1],4)) * gbar_K * E_K
            + g_leak * E_leak
            + gbar_M * p[i - 1] * E_K
            + I[i - 1] //* I_scale
            + nois_fact * distribution(generator) / (pow(dt,0.5))
        ) / (tau_V_inv * C);
        V[i] = V_inf + (V[i - 1] - V_inf) * exp(-dt * tau_V_inv);
        n[i] = n_inf(V[i]) + (n[i - 1] - n_inf(V[i])) * exp((-dt*T_adj_factor / tau_n(V[i])));
        m[i] = m_inf(V[i]) + (m[i - 1] - m_inf(V[i])) * exp((-dt*T_adj_factor / tau_m(V[i])));
        h[i] = h_inf(V[i]) + (h[i - 1] - h_inf(V[i])) * exp((-dt*T_adj_factor / tau_h(V[i])));
        p[i] = p_inf(V[i]) + (p[i - 1] - p_inf(V[i])) * exp((-dt*T_adj_factor / tau_p(V[i])));
  }
  return V;
}

// Wrapper
// g++ -O3 -Wall -shared -std=c++11 -fPIC `python3 -m pybind11 --includes` HHsimulatorWrapper.c -o HHsimulatorWrapper`python3-config --extension-suffix`
py::array_t<double> wrapper(py::array_t<double> parameters, int random_seed, double V0, py::array_t<double> I, double dt, double tfin) {
  py::buffer_info buf1 = parameters.request();
  py::buffer_info buf2 = I.request();

  auto V = py::array_t<double>(buf2.size);
  py::buffer_info buf3 = V.request();

  // double * parameters_ = (double *) buf1.ptr;
  // double * I_ = (double *) buf2.ptr;
  // double * V_out = (double *) buf3.ptr;
  double * parameters_ = static_cast<double *>(buf1.ptr);
  double * I_ = static_cast<double *>(buf2.ptr);
  double * V_out = static_cast<double *>(buf3.ptr);

  V_out = runHH(parameters_, random_seed, V0, I_, dt, tfin);
  V = py::array_t<double>(buf3.size, V_out);

    return V;
}

PYBIND11_MODULE(HHsimulatorWrapper_deprecated, m) {
    m.doc() = "HHSimulator Python Wrapper"; // optional module docstring

    m.def("runHH", &wrapper, "run HH Simulator");
}
