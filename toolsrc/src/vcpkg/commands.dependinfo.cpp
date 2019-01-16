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

    std::string create_dot_as_string(
        const std::vector<std::unique_ptr<SourceControlFile>>& source_control_files)
    {
        int empty_node_count = 0;

        std::string s;
        s.append("digraph G{ rankdir=LR; edge [minlen=3]; overlap=false;");

        for (const auto& source_control_file : source_control_files)
        {
            const SourceParagraph& source_paragraph = *source_control_file->core_paragraph;
            if (source_paragraph.depends.empty())
            {
                empty_node_count++;
                continue;
            }

            const std::string name = replace_dashes_with_underscore(source_paragraph.name);
            s.append(Strings::format("%s;", name));
            for (const Dependency& d : source_paragraph.depends)
            {
                const std::string dependency_name = replace_dashes_with_underscore(d.name());
                s.append(Strings::format("%s -> %s;", name, dependency_name));
            }
        }

        s.append(Strings::format("empty [label=\"%d singletons...\"]; }", empty_node_count));
        return s;
    }

    std::string create_dgml_as_string(
        const std::vector<std::unique_ptr<SourceControlFile>>& source_control_files)
    {
        std::string s;
        s.append("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
        s.append("<DirectedGraph xmlns=\"http://schemas.microsoft.com/vs/2009/dgml\">");

        std::string nodes, links;
        for (const auto& source_control_file : source_control_files)
        {
            const SourceParagraph& source_paragraph = *source_control_file->core_paragraph;
            const std::string name = source_paragraph.name;
            nodes.append(Strings::format("<Node Id=\"%s\" />", name));

            // Iterate over dependencies.
            for (const Dependency& d : source_paragraph.depends)
            {
                links.append(Strings::format("<Link Source=\"%s\" Target=\"%s\" />", name, d.name()));
            }

            // Iterate over feature dependencies.
            const std::vector<std::unique_ptr<FeatureParagraph>>& feature_paragraphs = source_control_file->feature_paragraphs;
            for (const auto& feature_paragraph : feature_paragraphs)
            {
                for (const Dependency& d : feature_paragraph->depends)
                {
                    links.append(Strings::format("<Link Source=\"%s\" Target=\"%s\" />", name, d.name()));
                }
            }
        }

        s.append(Strings::format("<Nodes>%s</Nodes>", nodes));

        s.append(Strings::format("<Links>%s</Links>", links));

        s.append("</DirectedGraph>");
        return s;
    }

    std::string create_graph_as_string(
        const std::unordered_set<std::string>& switches,
        const std::vector<std::unique_ptr<SourceControlFile>>& source_control_files)
    {
        if (Util::Sets::contains(switches, OPTION_DOT))
        {
            return create_dot_as_string(source_control_files);
        }
        else if (Util::Sets::contains(switches, OPTION_DGML))
        {
            return create_dgml_as_string(source_control_files);
        }
        return "";
    }

    void build_dependency_tree(std::map<std::string, std::vector<std::string>>& dependency_tree,
                               const std::vector<std::string>& dependency_names,
                               const std::vector<std::unique_ptr<SourceControlFile>>& source_control_files)
    {
        for (const auto& dependency_name : dependency_names)
        {
            if (!Util::Sets::contains(dependency_tree, dependency_name))
            {
                const auto port_file = Util::find_if(source_control_files, [&](const auto& source_control_file) {
                    return source_control_file->core_paragraph->name == dependency_name;
                });

                if (port_file != source_control_files.end())
                {
                    dependency_tree[dependency_name] = {};

                    const auto& port_source_paragraph = *(*port_file)->core_paragraph;

                    for (const auto& child_dependency : port_source_paragraph.depends)
                    {
                        dependency_tree[dependency_name].push_back(child_dependency.name());
                    }

                    build_dependency_tree(dependency_tree, dependency_tree[dependency_name], source_control_files);
                }
            }
        }
    }

    void perform_and_exit(const VcpkgCmdArguments& args, const VcpkgPaths& paths)
    {
        const ParsedArguments options = args.parse_arguments(COMMAND_STRUCTURE);

        auto source_control_files = Paragraphs::load_all_ports(paths.get_filesystem(), paths.ports);

        if (args.command_arguments.size() >= 1)
        {
            std::map<std::string, std::vector<std::string>> dependency_tree;
            build_dependency_tree(dependency_tree, args.command_arguments, source_control_files);

            if (!options.switches.empty())
            {
                const std::string graph_as_string = create_graph_as_string(options.switches, source_control_files);
                System::println(graph_as_string);
                Checks::exit_success(VCPKG_LINE_INFO);
            }

            for (const auto& dependency : dependency_tree)
            {
                const auto dependencies = Strings::join(", ", dependency.second);
                System::println("%s: %s", dependency.first, dependencies);
            }
        }
        else
        {
            if (!options.switches.empty())
            {
                const std::string graph_as_string = create_graph_as_string(options.switches, source_control_files);
                System::println(graph_as_string);
                Checks::exit_success(VCPKG_LINE_INFO);
            }

            for (auto&& source_control_file : source_control_files)
            {
                const SourceParagraph& source_paragraph = *source_control_file->core_paragraph;
                const auto s =
                    Strings::join(", ", source_paragraph.depends, [](const Dependency& d) { return d.name(); });
                System::println("%s: %s", source_paragraph.name, s);
            }
        }

        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
