/*
 * This source file is part of an OSTIS project. For the latest info, see http://ostis.net
 * Distributed under the MIT License
 * (See accompanying file COPYING.MIT or copy at http://opensource.org/licenses/MIT)
 */

#include "TemplateExpressionNode.hpp"

#include "sc-agents-common/utils/GenerationUtils.hpp"

TemplateExpressionNode::TemplateExpressionNode(
    ScMemoryContext * context,
    ScAddr const & formulaTemplate,
    std::shared_ptr<TemplateSearcherAbstract> templateSearcher)
  : context(context)
  , formulaTemplate(formulaTemplate)
  , templateSearcher(std::move(templateSearcher))
{
}

TemplateExpressionNode::TemplateExpressionNode(
    ScMemoryContext * context,
    ScAddr const & formulaTemplate,
    std::shared_ptr<TemplateSearcherAbstract> templateSearcher,
    std::shared_ptr<TemplateManagerAbstract> templateManager,
    std::shared_ptr<SolutionTreeManager> solutionTreeManager,
    ScAddr const & outputStructure,
    ScAddr const & formula)
  : context(context)
  , formulaTemplate(formulaTemplate)
  , templateSearcher(std::move(templateSearcher))
  , templateManager(std::move(templateManager))
  , solutionTreeManager(std::move(solutionTreeManager))
  , outputStructure(outputStructure)
  , formula(formula)
{
}

LogicExpressionResult TemplateExpressionNode::check(ScTemplateParams & params) const
{
  Replacements const & searchResult = templateSearcher->searchTemplate(formulaTemplate, params);
  std::string result = (!searchResult.empty() ? "true" : "false");
  SC_LOG_DEBUG("Atomic logical formula " + context->HelperGetSystemIdtf(formulaTemplate) + " " + result);

  return {true, true, searchResult, formulaTemplate};
}

LogicFormulaResult TemplateExpressionNode::compute(LogicFormulaResult & result) const
{
  std::string const formulaIdentifier = context->HelperGetSystemIdtf(formulaTemplate);
  SC_LOG_DEBUG("Checking atomic logical formula " + formulaIdentifier);

  Replacements replacements;
  // Template params should be created only if argument vector is not empty. Else search with any possible replacements
  if (!argumentVector.empty())
  {
    std::vector<ScTemplateParams> const & templateParamsVector = templateManager->createTemplateParams(formulaTemplate, argumentVector);
    replacements = templateSearcher->searchTemplate(formulaTemplate, templateParamsVector);
  }
  else
  {
    replacements = templateSearcher->searchTemplate(formulaTemplate, ScTemplateParams());
  }

  result.replacements = replacements;
  result.value = !result.replacements.empty();
  std::string formulaValue = (result.value ? " true" : " false");
  SC_LOG_DEBUG("Compute atomic logical formula " + formulaIdentifier + formulaValue);

  return result;
}

LogicFormulaResult TemplateExpressionNode::find(Replacements & replacements) const
{
  LogicFormulaResult result;
  std::vector<ScTemplateParams> paramsVector = ReplacementsUtils::getReplacementsToScTemplateParams(replacements);
  result.replacements = templateSearcher->searchTemplate(formulaTemplate, paramsVector);
  result.value = !result.replacements.empty();

  std::string const idtf = context->HelperGetSystemIdtf(formulaTemplate);
  std::string ending = (result.value ? " true" : " false");
  SC_LOG_DEBUG("Find Statement " + idtf + ending);

  return result;
}

/**
 * @brief Generate atomic logical formula using replacements
 * @param replacements variables and ScAddrs to use in generation
 * @return LogicFormulaResult{bool: value, bool: isGenerated, Replacements: replacements}
 */
LogicFormulaResult TemplateExpressionNode::generate(Replacements & replacements) const
{
  LogicFormulaResult result;
  result.isGenerated = false;

  // Convert replacements to templateParams to generate by them
  std::vector<ScTemplateParams> paramsVector = ReplacementsUtils::getReplacementsToScTemplateParams(replacements);

  std::set<std::string> const & replacementVarNames = ReplacementsUtils::getKeySet(replacements);
  std::set<std::string> const & templateVarNames = templateSearcher->getVarNames(formulaTemplate);
  std::set<std::string> varNames;
  varNames.insert(replacementVarNames.cbegin(), replacementVarNames.cend());
  varNames.insert(templateVarNames.cbegin(), templateVarNames.cend());
  if (paramsVector.empty())
  {
    SC_LOG_DEBUG("Atomic logical formula " << context->HelperGetSystemIdtf(formulaTemplate) << " is not generated");
    return compute(result);
  }

  size_t count = 0;
  for (ScTemplateParams const & scTemplateParams : paramsVector)
  {
    if (generateOnlyFirst && result.isGenerated)
      break;

    Replacements const & searchResult =
        templateSearcher->searchTemplate(formulaTemplate, scTemplateParams);
    if (searchResult.empty())
    {
      ScTemplate generatedTemplate;
      context->HelperBuildTemplate(generatedTemplate, formulaTemplate, scTemplateParams);

      ScTemplateGenResult generationResult;
      ScTemplate::Result const & genTemplate = context->HelperGenTemplate(generatedTemplate, generationResult);
      if (genTemplate)
      {
        ++count;
        result.isGenerated = true;
        result.value = true;
        Replacements temporalReplacements;
        for (std::string const & name : varNames)
        {
          ScAddrVector replacementsVector;
          bool const generationHasVar =
              generationResult.GetReplacements().find(name) != generationResult.GetReplacements().cend();
          ScAddr outResult;
          bool const paramsHaveVar = scTemplateParams.Get(name, outResult);
          if (generationHasVar)
            replacementsVector.push_back(generationResult[name]);
          else if (paramsHaveVar)
            replacementsVector.push_back(outResult);
          else
            SC_THROW_EXCEPTION(utils::ExceptionInvalidState, "generation result and template params do not have replacement for " << name);
          temporalReplacements[name] = replacementsVector;
        }
        result.replacements = ReplacementsUtils::uniteReplacements(result.replacements, temporalReplacements);
      }

      for (size_t i = 0; i < generationResult.Size(); ++i)
      {
        // TODO(MksmOrlov): support two implementations: with and without addition generated constructions to params
        // templateSearcher->addParamIfNotPresent(generationResult[i]);
        utils::GenerationUtils::addToSet(context, outputStructure, generationResult[i]);
      }
    }
  }

  SC_LOG_DEBUG("Atomic logical formula " << context->HelperGetSystemIdtf(formulaTemplate) << " is generated " << count << " times");

  return result;
}
