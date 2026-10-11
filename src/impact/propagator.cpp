#include "quantMicrostructure/impact/propagator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace quantMicrostructure
{

    std::vector<double> powerLawPropagator(double gamma0, double l0, double beta, std::size_t length)
    {
        if (!(l0 > 0.0))
        {
            throw std::invalid_argument("powerLawPropagator: l0 must be positive");
        }
        std::vector<double> g;
        g.reserve(length);
        for (std::size_t l = 1; l <= length; ++l)
        {
            g.push_back(gamma0 * std::pow(l0 / (l0 + static_cast<double>(l)), beta));
        }
        return g;
    }

    double criticalPropagatorExponent(double gamma)
    {
        return 0.5 * (1.0 - gamma);
    }

    std::vector<double> propagatorPrices(std::span<const double> signs, std::span<const double> propagator, double noise_sd, Rng &rng)
    {
        if (propagator.empty())
        {
            throw std::invalid_argument("propagatorPrices: the propagator is empty");
        }
        // K(0) = G(1), K(m) = G(m + 1) - G(m): the price change after a trade
        // is the convolution of K with the past signs.
        std::vector<double> kernel(propagator.size());
        kernel[0] = propagator[0];
        for (std::size_t m = 1; m < propagator.size(); ++m)
        {
            kernel[m] = propagator[m] - propagator[m - 1];
        }
        std::vector<double> prices(signs.size(), 0.0);
        for (std::size_t n = 0; n + 1 < signs.size(); ++n)
        {
            double change = noise_sd > 0.0 ? noise_sd * rng.normal() : 0.0;
            const std::size_t reach = std::min(kernel.size(), n + 1);
            for (std::size_t m = 0; m < reach; ++m)
            {
                change += kernel[m] * signs[n - m];
            }
            prices[n + 1] = prices[n] + change;
        }
        return prices;
    }

    std::vector<double> priceDiffusion(std::span<const double> prices, std::span<const std::size_t> lags)
    {
        std::vector<double> out;
        out.reserve(lags.size());
        for (const std::size_t lag : lags)
        {
            if (lag >= prices.size())
            {
                throw std::invalid_argument("priceDiffusion: a lag is not shorter than the sample");
            }
            double sum = 0.0;
            for (std::size_t n = 0; n + lag < prices.size(); ++n)
            {
                const double change = prices[n + lag] - prices[n];
                sum += change * change;
            }
            out.push_back(sum / static_cast<double>(prices.size() - lag));
        }
        return out;
    }

    std::vector<double> fitPropagator(std::span<const double> signs, std::span<const double> prices, std::size_t length)
    {
        const std::size_t n = signs.size();
        if (prices.size() != n)
        {
            throw std::invalid_argument("fitPropagator: signs and prices must have the same length");
        }
        if (length == 0 || n < 2 * length + 2)
        {
            throw std::invalid_argument("fitPropagator: too few trades for this length");
        }
        const std::size_t changes = n - 1;

        // C(k) = <e_{i+k} e_i> and S(l) = <r_{i+l} e_i>.
        std::vector<double> c(length, 0.0);
        std::vector<double> s(length, 0.0);
        for (std::size_t k = 0; k < length; ++k)
        {
            double sum_c = 0.0;
            for (std::size_t i = 0; i + k < n; ++i)
            {
                sum_c += signs[i + k] * signs[i];
            }
            c[k] = sum_c / static_cast<double>(n - k);
            double sum_s = 0.0;
            for (std::size_t i = 0; i + k < changes; ++i)
            {
                sum_s += (prices[i + k + 1] - prices[i + k]) * signs[i];
            }
            s[k] = sum_s / static_cast<double>(changes - k);
        }

        // Solve sum_m C(|l - m|) K(m) = S(l) by Gaussian elimination with
        // partial pivoting.
        std::vector<double> a(length * length);
        for (std::size_t l = 0; l < length; ++l)
        {
            for (std::size_t m = 0; m < length; ++m)
            {
                a[l * length + m] = c[l > m ? l - m : m - l];
            }
        }
        std::vector<double> k = s;
        for (std::size_t col = 0; col < length; ++col)
        {
            std::size_t pivot = col;
            for (std::size_t row = col + 1; row < length; ++row)
            {
                if (std::abs(a[row * length + col]) > std::abs(a[pivot * length + col]))
                {
                    pivot = row;
                }
            }
            if (std::abs(a[pivot * length + col]) < 1e-12)
            {
                throw std::invalid_argument("fitPropagator: the sign correlations do not determine a propagator");
            }
            if (pivot != col)
            {
                for (std::size_t j = 0; j < length; ++j)
                {
                    std::swap(a[col * length + j], a[pivot * length + j]);
                }
                std::swap(k[col], k[pivot]);
            }
            for (std::size_t row = col + 1; row < length; ++row)
            {
                const double factor = a[row * length + col] / a[col * length + col];
                if (factor == 0.0)
                {
                    continue;
                }
                for (std::size_t j = col; j < length; ++j)
                {
                    a[row * length + j] -= factor * a[col * length + j];
                }
                k[row] -= factor * k[col];
            }
        }
        for (std::size_t row = length; row-- > 0;)
        {
            for (std::size_t j = row + 1; j < length; ++j)
            {
                k[row] -= a[row * length + j] * k[j];
            }
            k[row] /= a[row * length + row];
        }

        std::vector<double> g(length);
        double sum = 0.0;
        for (std::size_t l = 0; l < length; ++l)
        {
            sum += k[l];
            g[l] = sum;
        }
        return g;
    }

} // namespace quantMicrostructure
