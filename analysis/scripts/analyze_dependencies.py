#!/usr/bin/env python3
"""
3Com Network Driver Dependency Analysis Tool

Analyzes the 3Com packet driver codebase to identify:
- Live vs orphaned code
- Integration gaps for DMA safety and cache coherency
- Header correction impact
- C to Assembly cross-references
"""

import re
import os
import sys
from pathlib import Path
from collections import defaultdict
import json

class DriverDependencyAnalyzer:
    def __init__(self, project_root):
        self.project_root = Path(project_root)
        self.src_dir = self.project_root / 'src'
        self.include_dir = self.project_root / 'include'
        
        self.dependencies = {
            'c_to_asm': defaultdict(list),
            'asm_to_c': defaultdict(list),
            'header_usage': defaultdict(list),
            'orphaned_refs': [],
            'integration_gaps': [],
            'header_corrections': [],
            'live_files': set(),
            'orphaned_files': set(),
            'pci_files': set()
        }
        
        # Core live driver files (actively used)
        self.core_files = {
            '3c509b.c', '3c515.c', 'nic_init.c', 'api.c', 'packet_ops.c',
            'buffer_alloc.c', 'memory.c', 'config.c', 'stats.c', 'promisc.c',
            'media_control.c', 'hardware.c', 'packet_api.asm', 'nic_irq.asm',
            'hardware.asm'
        }
        
        # Integration features (orphaned but should be integrated)
        self.integration_files = {
            'dma_safety.c', 'cache_coherency.c', 'cache_management.c',
            'runtime_config.c', 'xms_buffer_migration.c', 'handle_compact.c',
            'multi_nic_coord.c', 'cache_ops.asm'
        }
        
        # PCI variants
        self.pci_files = {
            '3com_vortex.c', '3com_boomerang.c', '3com_init.c',
            '3com_power.c', '3com_performance.c', '3c900tpo.c'
        }

    def analyze_watcom_nasm_deps(self):
        """Parse Watcom C files for assembly interfaces and extern calls"""
        print("Analyzing C to Assembly dependencies...")
        
        for c_file in self.src_dir.glob('**/*.c'):
            if not c_file.exists():
                continue
                
            filename = c_file.name
            try:
                with open(c_file, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()
                    
                    # Find Watcom pragma aux (assembly interfaces)
                    pragma_aux = re.findall(r'#pragma\s+aux\s+(\w+)', content)
                    
                    # Find extern function declarations (potential ASM calls)
                    externs = re.findall(r'extern\s+\w+\s*\**\s+(\w+)\s*\(', content)
                    
                    # Find function calls that might be to ASM functions
                    asm_calls = re.findall(r'(\w+)\s*\(', content)
                    asm_calls = [call for call in asm_calls if call.endswith('_asm') or 
                               call.startswith('asm_') or call in pragma_aux]
                    
                    # Check for references to orphaned features
                    if any(feature in content.lower() for feature in 
                          ['dma_safety', 'cache_coherency', 'runtime_config']):
                        self.dependencies['orphaned_refs'].append({
                            'file': filename,
                            'features': self._find_orphaned_features(content),
                            'type': 'Reference without integration'
                        })
                    
                    # Check for incorrect header usage
                    incorrect_patterns = [
                        r'_3C509B_PRODUCT_ID(?!\w)',  # Should be MANUFACTURER_ID
                        r'_3C509B_TXSTAT_COMPLETE.*0x01',  # Wrong bit value
                        r'_3C509B_MEDIA_TP.*0x00C0',  # Wrong media bits
                        r'_3C509B_RX_FILTER_PROM(?!\w)',  # Should be PROMISCUOUS
                    ]
                    
                    for pattern in incorrect_patterns:
                        matches = re.findall(pattern, content)
                        if matches:
                            self.dependencies['header_corrections'].append({
                                'file': filename,
                                'pattern': pattern,
                                'matches': len(matches)
                            })
                    
                    self.dependencies['c_to_asm'][filename] = {
                        'pragma_aux': pragma_aux,
                        'externs': externs,
                        'asm_calls': asm_calls
                    }
                    
                    # Categorize files
                    if filename in self.core_files:
                        self.dependencies['live_files'].add(filename)
                    elif filename in self.integration_files:
                        self.dependencies['orphaned_files'].add(filename)
                    elif filename in self.pci_files:
                        self.dependencies['pci_files'].add(filename)
            
            except Exception as e:
                print(f"Error processing {c_file}: {e}")

    def analyze_asm_files(self):
        """Parse NASM assembly files"""
        print("Analyzing Assembly to C dependencies...")
        
        for asm_file in self.src_dir.glob('**/*.asm'):
            if not asm_file.exists():
                continue
                
            filename = asm_file.name
            try:
                with open(asm_file, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()
                    
                    # Find GLOBAL/PUBLIC exports
                    exports = re.findall(r'(?:GLOBAL|global|PUBLIC|public)\s+(\w+)', content)
                    
                    # Find C function calls (Watcom C name mangling)
                    c_calls = re.findall(r'call\s+_(\w+)', content)
                    
                    # Find external references
                    externals = re.findall(r'(?:EXTERN|extern)\s+_?(\w+)', content)
                    
                    self.dependencies['asm_to_c'][filename] = {
                        'exports': exports,
                        'c_calls': c_calls,
                        'externals': externals
                    }
                    
                    # Categorize ASM files
                    if filename in self.core_files:
                        self.dependencies['live_files'].add(filename)
                    elif filename in self.integration_files:
                        self.dependencies['orphaned_files'].add(filename)
                        
            except Exception as e:
                print(f"Error processing {asm_file}: {e}")

    def analyze_header_usage(self):
        """Analyze header file dependencies"""
        print("Analyzing header dependencies...")
        
        for header_file in self.include_dir.glob('*.h'):
            if not header_file.exists():
                continue
                
            header_name = header_file.name
            
            # Find all files that include this header
            for source_file in list(self.src_dir.glob('**/*.c')) + list(self.src_dir.glob('**/*.asm')):
                try:
                    with open(source_file, 'r', encoding='utf-8', errors='ignore') as f:
                        content = f.read()
                        if f'#include "{header_name}"' in content or f'#include <{header_name}>' in content:
                            self.dependencies['header_usage'][header_name].append(source_file.name)
                except:
                    continue

    def find_integration_gaps(self):
        """Identify where orphaned features should be integrated"""
        print("Finding integration gaps...")
        
        # DMA safety integration points
        dma_integration_points = [
            {'file': '3c515.c', 'function': 'transmit_packet', 'reason': 'DMA boundary check needed'},
            {'file': 'packet_ops.c', 'function': 'dma_receive', 'reason': 'Cache coherency required'},
            {'file': 'buffer_alloc.c', 'function': 'allocate_dma_buffer', 'reason': 'DMA safety validation'}
        ]
        
        # Runtime configuration integration points  
        runtime_config_points = [
            {'file': 'config.c', 'function': 'load_config', 'reason': 'Should support runtime changes'},
            {'file': 'media_control.c', 'function': 'set_media_type', 'reason': 'Runtime media switching'}
        ]
        
        self.dependencies['integration_gaps'] = {
            'dma_safety': dma_integration_points,
            'runtime_config': runtime_config_points
        }

    def _find_orphaned_features(self, content):
        """Find specific orphaned feature references in content"""
        features = []
        if 'dma_safety' in content.lower():
            features.append('DMA Safety')
        if 'cache_coherency' in content.lower():
            features.append('Cache Coherency')
        if 'runtime_config' in content.lower():
            features.append('Runtime Configuration')
        return features

    def generate_dot_graph(self, graph_type='overview'):
        """Generate DOT format dependency graphs"""
        if graph_type == 'overview':
            return self._generate_overview_graph()
        elif graph_type == 'integration_gaps':
            return self._generate_integration_gaps_graph()
        elif graph_type == 'header_corrections':
            return self._generate_header_corrections_graph()

    def _generate_overview_graph(self):
        """Generate overview dependency graph"""
        dot_content = """digraph DriverDependencies {
    rankdir=LR;
    node [shape=box];
    
    subgraph cluster_live {
        label="Live Driver Code";
        color=green;
        style=filled;
        fillcolor=lightgreen;
        
"""
        for file in sorted(self.dependencies['live_files']):
            dot_content += f'        "{file}" [color=green];\n'
        
        dot_content += """    }
    
    subgraph cluster_orphaned {
        label="Orphaned Features";
        color=red;
        style=dashed;
        fillcolor=lightpink;
        
"""
        for file in sorted(self.dependencies['orphaned_files']):
            dot_content += f'        "{file}" [color=red, style=dashed];\n'
            
        dot_content += """    }
    
    subgraph cluster_pci {
        label="PCI Variants";
        color=blue;
        style=dotted;
        fillcolor=lightblue;
        
"""
        for file in sorted(self.dependencies['pci_files']):
            dot_content += f'        "{file}" [color=blue, style=dotted];\n'
            
        dot_content += """    }
    
    // Add cross-references
"""
        # Add C to ASM dependencies
        for c_file, asm_deps in self.dependencies['c_to_asm'].items():
            if c_file in self.dependencies['live_files']:
                for asm_call in asm_deps.get('asm_calls', []):
                    dot_content += f'    "{c_file}" -> "{asm_call}" [label="calls"];\n'

        dot_content += "}\n"
        return dot_content

    def _generate_integration_gaps_graph(self):
        """Generate graph showing integration gaps"""
        dot_content = """digraph IntegrationGaps {
    rankdir=TB;
    node [shape=box];
    
    subgraph cluster_current {
        label="Current Implementation";
        color=green;
        
        "3c515.c" -> "memory.c";
        "3c515.c" -> "buffer_alloc.c";
        "packet_ops.c" -> "hardware.c";
    }
    
    subgraph cluster_missing {
        label="Missing Integration";
        color=red;
        style=dashed;
        
        "dma_safety.c" [color=red];
        "cache_coherency.c" [color=red];
        "runtime_config.c" [color=red];
    }
    
    // Show where integration should happen
    "3c515.c" -> "dma_safety.c" [label="NEEDED", color=orange, style=dashed];
    "packet_ops.c" -> "cache_coherency.c" [label="NEEDED", color=orange, style=dashed];
    "config.c" -> "runtime_config.c" [label="UPGRADE", color=blue, style=dashed];
    
}
"""
        return dot_content

    def _generate_header_corrections_graph(self):
        """Generate graph showing header correction impact"""
        dot_content = """digraph HeaderCorrections {
    rankdir=TB;
    node [shape=box];
    
    "3c509b.h" [label="3c509b.h\\n(CORRECTED)", color=green, style=bold];
    
"""
        # Show files that need updates due to header corrections
        for correction in self.dependencies['header_corrections']:
            file = correction['file']
            matches = correction['matches']
            dot_content += f'    "3c509b.h" -> "{file}" [label="Fix {matches} refs", color=red];\n'
        
        dot_content += "}\n"
        return dot_content

    def run_analysis(self):
        """Run complete dependency analysis"""
        print(f"Starting dependency analysis for {self.project_root}")
        
        self.analyze_watcom_nasm_deps()
        self.analyze_asm_files()
        self.analyze_header_usage()
        self.find_integration_gaps()
        
        return self.dependencies

    def save_results(self, output_dir):
        """Save analysis results"""
        output_path = Path(output_dir)
        output_path.mkdir(parents=True, exist_ok=True)
        
        # Save JSON report
        with open(output_path / 'dependency_analysis.json', 'w') as f:
            # Convert sets to lists for JSON serialization
            deps_copy = dict(self.dependencies)
            deps_copy['live_files'] = list(deps_copy['live_files'])
            deps_copy['orphaned_files'] = list(deps_copy['orphaned_files'])
            deps_copy['pci_files'] = list(deps_copy['pci_files'])
            json.dump(deps_copy, f, indent=2)
        
        # Save DOT graphs
        for graph_type in ['overview', 'integration_gaps', 'header_corrections']:
            dot_content = self.generate_dot_graph(graph_type)
            with open(output_path / f'{graph_type}.dot', 'w') as f:
                f.write(dot_content)
        
        print(f"Analysis results saved to {output_path}")

def main():
    if len(sys.argv) != 2:
        print("Usage: python analyze_dependencies.py <project_root>")
        sys.exit(1)
    
    project_root = sys.argv[1]
    analyzer = DriverDependencyAnalyzer(project_root)
    
    dependencies = analyzer.run_analysis()
    analyzer.save_results(Path(project_root) / 'analysis' / 'reports')
    
    # Print summary
    print("\n" + "="*60)
    print("DEPENDENCY ANALYSIS SUMMARY")
    print("="*60)
    print(f"Live files: {len(dependencies['live_files'])}")
    print(f"Orphaned files: {len(dependencies['orphaned_files'])}")
    print(f"PCI variant files: {len(dependencies['pci_files'])}")
    print(f"Header corrections needed: {len(dependencies['header_corrections'])}")
    print(f"Integration gaps identified: {len(dependencies['integration_gaps'])}")
    
    if dependencies['header_corrections']:
        print("\nFILES NEEDING HEADER CORRECTIONS:")
        for correction in dependencies['header_corrections']:
            print(f"  - {correction['file']}: {correction['matches']} fixes needed")

if __name__ == "__main__":
    main()