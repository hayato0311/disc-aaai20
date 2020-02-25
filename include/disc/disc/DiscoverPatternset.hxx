#pragma once

#include <disc/disc/Composition.hxx>
#include <disc/disc/PatternsetResult.hxx>
#include <disc/disc/SlimCandidateGeneration.hxx>
#include <disc/disc/Encoding.hxx>
#include <disc/disc/HeuristicScore.hxx>
#include <disc/disc/InsertMissingSingletons.hxx>
#include <disc/distribution/Distribution.hxx>
#include <disc/utilities/EmptyCallback.hxx>
#include <disc/utilities/PvalueNHC.hxx>
#include <disc/utilities/Support.hxx>

#include <nonstd/optional.hpp>

#include <algorithm>
#include <chrono>


namespace sd
{
namespace disc
{

template <typename T, typename Candidate, typename Distribution>
auto single_component_expected_gain(size_t data_size, const Candidate& x, const Distribution& p)
    -> T
{
    const auto fr = T(x.support) / data_size;
    const auto e  = p.expected_frequency(x.pattern);
    if(std::abs(fr - e) < 0.04) return T(0);
    return fr * kl1(fr, e);
}

template <typename Trait, typename Candidate, typename Distribution, typename Float>
bool is_candidate_significant(PatternsetResult<Trait>& c,
                              const Candidate&         x,
                              const Distribution&      p,
                              const MiningSettings&    cfg,
                              const Float              add_bic_cost)
{
    // return true;
    using float_type = typename Trait::float_type;
    // return nhc_pvalue<float_type>(0, x.score) > float_type(1) - cfg.alpha;
    // const auto fr = float_type(x.support) / c.data.size();
    // return std::abs(fr - p.expected_frequency(x.pattern)) > 0.01;

    const auto fr  = float_type(x.support) / c.data.size();
    const auto mu  = p.expected_frequency(x.pattern);
    const auto dkl = kl1(fr, mu);
    // const auto g   = fr * dkl * c.data.size();
    const auto g   = dkl * x.support;

    // auto g = single_component_expected_gain<float_type>(c.data.size(), x, p);
    auto r = cfg.use_bic ? add_bic_cost : additional_cost_mdl(c, x.pattern, x.support);

    return nhc_pvalue<float_type>(r, g) > float_type(1) - cfg.alpha;
}

template <typename Trait>
typename Trait::distribution_type& initialize_model(PatternsetResult<Trait>& com,
                                                    const MiningSettings&    cfg = {})
{
    auto& data    = com.data;
    auto& summary = com.summary;

    if (cfg.with_singletons)
    {
        insert_missing_singletons(data, summary);
    }

    com.model = make_distribution(com, cfg);
    auto& pr  = com.model.value();

    for (const auto& i : summary)
    {
        pr.insert(label(i), point(i), false);
    }
    estimate_model(pr);

    com.initial_encoding = encoding_length_sdm(pr, data, summary, cfg.use_bic);

    return pr;
}

template <typename Trait, typename GainFunction, typename CALLBACK = EmptyCallback>
PatternsetResult<Trait> discover_patternset(PatternsetResult<Trait> s,
                                            GainFunction&&          expected_gain,
                                            const MiningSettings&   cfg,
                                            CALLBACK&&              callback = {})
{
    using patter_type    = typename Trait::pattern_type;
    using float_type     = typename Trait::float_type;
    using generator_type = SlimGenerator<patter_type, float_type>;

    assert(s.data.dim != 0);

    auto&      pr           = initialize_model(s, cfg);
    const auto add_bic_cost = additional_cost_bic(s);

    auto score_fn = [&](const auto& x) {
        return expected_gain(s, x, add_bic_cost);
    };

    auto gen = generator_type(s.data, cfg.min_support, cfg.max_pattern_size, score_fn);

    size_t items_used = 0;
    size_t patience   = cfg.max_patience;

    const auto start_time = std::chrono::high_resolution_clock::now();

    for (size_t it = 0; it < cfg.max_iteration; ++it)
    {
        if (!gen.has_next())
            break;

        if (auto c = gen.next(); c && is_candidate_significant(s, *c, pr, cfg, add_bic_cost))
        {
            items_used = items_used + 1;

            const auto fr = float_type(c->support) / s.data.size();
            pr.insert(fr, c->pattern, true);
            s.summary.insert(fr, c->pattern);

            patience = std::max(patience * 2, cfg.max_patience);

            gen.add_next(*c, score_fn);

            // std::cout << gen.count_current_candidates() << " #candidates before pruning\n";

            gen.prune(
                [&](const auto& t) { return t.score <= 0 || !pr.is_item_allowed(t.pattern); });

            // std::cout << gen.count_current_candidates() << " #candidates after pruning\n";
            // std::cout.flush();

            callback(std::as_const(s));
        }
        else if (patience-- == 0)
            break;

        if (cfg.max_time &&
            std::chrono::high_resolution_clock::now() - start_time > *cfg.max_time)
        {
            break;
        }

        if (cfg.max_patternset_size && items_used > *cfg.max_patternset_size)
        {
            break;
        }
    }

    s.encoding = encoding_length_sdm(pr, s.data, s.summary, cfg.use_bic);

    callback(std::as_const(s));

    return s;
}

template <typename Trait, typename CALLBACK = EmptyCallback>
PatternsetResult<Trait> discover_patternset(PatternsetResult<Trait> s,
                                            const MiningSettings&   cfg,
                                            CALLBACK&&              callback = {})
{
    auto h = [use_bic = cfg.use_bic](
                 const PatternsetResult<Trait>& s, const auto& x, const auto bic_const) {
        using float_t = typename Trait::float_type;

        auto gain = single_component_expected_gain<float_t>(s.data.size(), x, s.model.value());
        auto cost = use_bic ? bic_const : additional_cost_mdl(s, x.pattern, x.support);
        return gain * s.data.size() - cost;
        // const auto d = s.model->expected_frequency(x.pattern) - float_t(x.support) / s.data.size();
        // return std::sqrt(d * d) - 0.05;
    };
    return discover_patternset(std::move(s), h, cfg, std::forward<CALLBACK>(callback));
}

template <typename Trait, typename CALLBACK = EmptyCallback>
Composition<Trait> discover_patternset(Composition<Trait>    c,
                                       const MiningSettings& cfg      = {},
                                       CALLBACK&&            callback = {})
{
    PatternsetResult<Trait> s;
    s.data    = std::move(c.data);
    s.summary = std::move(c.summary);
    s         = discover_patternset(std::move(s), cfg, std::forward<CALLBACK>(callback));
    return Composition<Trait>(s);
}

} // namespace disc
} // namespace sd
