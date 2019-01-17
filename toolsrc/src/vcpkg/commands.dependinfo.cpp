#include "pch.h"

#include <vcpkg/base/strings.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/util.h>
#include <vcpkg/commands.h>
#include <vcpkg/help.h>
#include <vcpkg/paragraphs.h>

namespace vcpkg::Commands::DependInfo
{
    constexpr StringLiteral OPTION_DOT = "--dot";
    constexpr StringLiteral OPTION_DGML = "--dgml";

    constexpr std::array<CommandSwitch, 2> DEPEND_SWITCHES = { {
        { OPTION_DOT, "Creates graph on basis of dot" },
        { OPTION_DGML, "Creates graph on basis of dgml" },
        } };

    const CommandStructure COMMAND_STRUCTURE = {
        Help::create_example_string(R"###(depend-info [pat])###"),
        0,
        SIZE_MAX,
        {DEPEND_SWITCHES, {}},
        nullptr,
    };

    std::string replace_dashes_with_underscore(const std::string& input)
    {
        std::string output = input;
        std::replace(output.begin(), output.end(), '-', '_');
        return output;
    }

    std::string create_dot_as_string(std::map<std::string, std::vector<std::string>>& packages)
    {
        int empty_node_count = 0;

        std::string s;
        s.append("digraph G{ rankdir=LR; edge [minlen=3]; overlap=false;");

        for (const auto& package : packages)
        {
            if (package.second.empty())
            {
                empty_node_count++;
                continue;
            }

            const std::string package_name = replace_dashes_with_underscore(package.first);
            s.append(Strings::format("%s;", package_name));
            for (const auto& dependency : package.second)
            {
                const auto dependency_name = replace_dashes_with_underscore(dependency);
                s.append(Strings::format("%s -> %s;", package_name, dependency_name));
            }
        }

        s.append(Strings::format("empty [label=\"%d singletons...\"]; }", empty_node_count));
        return s;
    }

    std::string create_dgml_as_string(std::map<std::string, std::vector<std::string>>& packages)
    {
        std::string s;
        s.append("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
        s.append("<DirectedGraph xmlns=\"http://schemas.microsoft.com/vs/2009/dgml\">");

        std::string nodes, links;
        for (const auto& package : packages)
        {
            const auto package_name = package.first;
            nodes.append(Strings::format("<Node Id=\"%s\" />", package_name));

            // Iterate over dependencies.
            for (const auto& dependency : package.second)
            {
                links.append(Strings::format("<Link Source=\"%s\" Target=\"%s\" />", package_name, dependency));
            }
        }

        s.append(Strings::format("<Nodes>%s</Nodes>", nodes));

        s.append(Strings::format("<Links>%s</Links>", links));

        s.append("</DirectedGraph>");
        return s;
    }

    std::string create_graph_as_string(const std::unordered_set<std::string>& switches,
                                       std::map<std::string, std::vector<std::string>>& packages)
    {
        if (Util::Sets::contains(switches, OPTION_DOT))
        {
            return create_dot_as_string(packages);
        }
        else if (Util::Sets::contains(switches, OPTION_DGML))
        {
            return create_dgml_as_string(packages);
        }
        return "";
    }

    void build_packages_list_with_filter(std::map<std::string, std::vector<std::string>>& packages,
                                         const std::vector<std::string>& requested_package_names,
                                         const std::vector<std::unique_ptr<SourceControlFile>>& source_control_files)
    {
        for (const auto& package_name : requested_package_names)
        {
            if (!Util::Sets::contains(packages, package_name))
            {
                const auto source_control_file =
                    Util::find_if(source_control_files, [&package_name](const auto& source_control_file) {
                        return source_control_file->core_paragraph->name == package_name;
                    });

                if (source_control_file != source_control_files.end())
                {
                    packages[package_name] = {};

                    for (const auto& dependency : (*source_control_file)->core_paragraph->depends)
                    {
                        packages[package_name].push_back(dependency.name());
                    }

                    build_packages_list_with_filter(packages, packages[package_name], source_control_files);
                }
            }
        }
    }

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        const auto source_control_files = Paragraphs::load_all_ports(paths.get_filesystem(), paths.ports);
        std::map<std::string, std::vector<std::string>> packages;

        if (args.command_arguments.size() >= 1)
        {
            build_packages_list_with_filter(packages, args.command_arguments, source_control_files);
        }
        else
        {
            for (const auto& source_control_file : source_control_files)
            {
                const auto package_name = source_control_file->core_paragraph->name;
                packages[package_name] = {};

                for (const auto& dependency : source_control_file->core_paragraph->depends)
                {
                    packages[package_name].push_back(dependency.name());
                }
            }
        }

        if (!options.switches.empty())
        {
            const auto graph_as_string = create_graph_as_string(options.switches, packages);
            System::println(graph_as_string);
        }
        else
        {
            for (const auto& package : packages)
            {
                const auto package_name = package.first;
                const auto dependencies = Strings::join(", ", package.second);
                System::println("%s: %s", package_name, dependencies);
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
