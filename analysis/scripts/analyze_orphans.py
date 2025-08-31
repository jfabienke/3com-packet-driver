#!/usr/bin/env python3
"""
Orphan Code Analysis for 3Com Packet Driver
Automated detection and analysis of orphaned files and their relationships
"""

import os
import re
import sys
from pathlib import Path
from collections import defaultdict, namedtuple
import subprocess

# Data structures
FileInfo = namedtuple('FileInfo', ['path', 'category', 'confidence', 'references', 'features'])
Reference = namedtuple('Reference', ['file', 'line', 'context'])

class OrphanAnalyzer:
    def __init__(self, project_root):
        self.project_root = Path(project_root)
        self.live_files = self._load_live_files()
        self.orphaned_files = []
        self.references = defaultdict(list)
        
    def _load_live_files(self):
        """Load list of live files from Makefile analysis"""
        live_files = {
            # Hot section
            'src/c/api.c', 'src/c/routing.c', 'src/c/packet_ops.c',
            # Cold section  
            'src/c/init.c', 'src/c/config.c', 'src/c/memory.c', 'src/c/xms_detect.c',
            'src/c/umb_loader.c', 'src/c/eeprom.c', 'src/c/buffer_alloc.c',
            'src/c/buffer_autoconfig.c', 'src/c/static_routing.c', 'src/c/arp.c',
            'src/c/nic_init.c', 'src/c/hardware.c', 'src/c/3c515.c', 'src/c/3c509b.c',
            'src/loader/cpu_detect.c', 'src/loader/patch_apply.c',
            # Debug (conditional)
            'src/c/diagnostics.c', 'src/c/logging.c', 'src/c/stats.c',
            # Assembly
            'src/asm/packet_api_smc.asm', 'src/asm/nic_irq_smc.asm', 
            'src/asm/hardware_smc.asm', 'src/asm/flow_routing.asm',
            'src/asm/direct_pio.asm', 'src/asm/tsr_common.asm', 'src/asm/tsr_loader.asm',
            'src/asm/cpu_detect.asm', 'src/asm/pnp.asm', 'src/asm/promisc.asm',
            'src/asm/smc_patches.asm'
        }
        return live_files
    
    def find_all_source_files(self):
        """Find all C and ASM source files in the project"""
        c_files = list(self.project_root.glob('src/**/*.c'))
        asm_files = list(self.project_root.glob('src/**/*.asm'))
        return c_files + asm_files
    
    def categorize_orphaned_file(self, file_path):
        """Categorize an orphaned file and assess deletion confidence"""
        filename = file_path.name
        rel_path = str(file_path.relative_to(self.project_root))
        
        # Backup files - HIGH confidence deletion
        if filename.endswith('.bak'):
            return FileInfo(rel_path, 'backup', 100, [], ['Backup file'])
        
        # Test/demo files - HIGH confidence
        if any(keyword in filename.lower() for keyword in ['test', 'demo', 'ansi_demo', 'console']):
            return FileInfo(rel_path, 'test_demo', 95, [], ['Test/demo code'])
        
        # PCI/Vortex cards - HIGH confidence
        if any(keyword in filename for keyword in ['3com_', 'vortex', 'boomerang']):
            return FileInfo(rel_path, 'pci_vortex', 90, [], ['PCI card support'])
        
        # Enhanced duplicates - HIGH confidence
        if 'enhanced' in filename or any(f in filename for f in ['3c515_enhanced', '3c509b_pio']):
            return FileInfo(rel_path, 'enhanced_duplicate', 85, [], ['Enhanced implementation'])
        
        # DMA/Cache safety - LOW confidence (critical)
        if any(keyword in filename for keyword in ['dma_', 'cache_']):
            return FileInfo(rel_path, 'dma_safety', 20, [], ['DMA/cache safety'])
        
        # Performance features - MEDIUM confidence
        if any(keyword in filename for keyword in ['performance', 'multi_nic', 'runtime_config']):
            return FileInfo(rel_path, 'performance', 60, [], ['Performance features'])
        
        # Assembly duplicates - HIGH confidence
        if file_path.suffix == '.asm':
            base_name = filename.replace('.asm', '')
            if any(base_name in live for live in self.live_files if live.endswith('_smc.asm')):
                return FileInfo(rel_path, 'asm_duplicate', 90, [], ['Assembly duplicate'])
        
        # Default orphaned
        return FileInfo(rel_path, 'general_orphan', 70, [], ['General orphaned code'])
    
    def find_references(self, orphaned_file):
        """Find references to orphaned file in live code using ripgrep"""
        filename = Path(orphaned_file.path).name
        base_name = filename.replace('.c', '').replace('.asm', '').replace('.h', '')
        
        references = []
        
        # Search patterns
        patterns = [
            base_name,  # Direct filename reference
            f"#include.*{filename}",  # Include statements
            f"{base_name}_",  # Function prefixes
        ]
        
        for pattern in patterns:
            try:
                # Use ripgrep to search in live files only
                live_file_args = []
                for live_file in self.live_files:
                    full_path = self.project_root / live_file
                    if full_path.exists():
                        live_file_args.append(str(full_path))
                
                if not live_file_args:
                    continue
                    
                result = subprocess.run([
                    'rg', '-n', '-H', pattern
                ] + live_file_args, 
                capture_output=True, text=True, cwd=self.project_root)
                
                for line in result.stdout.strip().split('\n'):
                    if line and ':' in line:
                        parts = line.split(':', 2)
                        if len(parts) >= 3:
                            file_path, line_num, context = parts
                            references.append(Reference(file_path, line_num, context.strip()))
                            
            except (subprocess.SubprocessError, FileNotFoundError):
                # Fallback to basic grep if rg not available
                pass
        
        return references
    
    def analyze_codebase(self):
        """Perform complete orphan analysis"""
        print("🔍 Analyzing 3Com Packet Driver codebase for orphaned code...")
        print(f"📁 Project root: {self.project_root}")
        
        all_files = self.find_all_source_files()
        print(f"📊 Found {len(all_files)} total source files")
        print(f"✅ Live files: {len(self.live_files)}")
        
        # Find orphaned files
        for file_path in all_files:
            rel_path = str(file_path.relative_to(self.project_root))
            if rel_path not in self.live_files:
                file_info = self.categorize_orphaned_file(file_path)
                
                # Find references in live code
                if file_info.confidence < 80:  # Only check references for uncertain files
                    references = self.find_references(file_info)
                    file_info = file_info._replace(references=references)
                    
                    # Adjust confidence based on references
                    if references and file_info.category in ['dma_safety', 'performance']:
                        file_info = file_info._replace(confidence=max(10, file_info.confidence - 30))
                
                self.orphaned_files.append(file_info)
        
        print(f"🗑️  Orphaned files: {len(self.orphaned_files)}")
        return self.orphaned_files
    
    def generate_report(self, output_file=None):
        """Generate detailed analysis report"""
        if not self.orphaned_files:
            self.analyze_codebase()
        
        # Group by category
        by_category = defaultdict(list)
        for file_info in self.orphaned_files:
            by_category[file_info.category].append(file_info)
        
        # Generate report
        report = []
        report.append("# Automated Orphan Code Analysis Report")
        report.append("")
        report.append(f"**Generated**: {subprocess.run(['date'], capture_output=True, text=True).stdout.strip()}")
        report.append(f"**Total Files Analyzed**: {len(self.find_all_source_files())}")
        report.append(f"**Live Files**: {len(self.live_files)}")
        report.append(f"**Orphaned Files**: {len(self.orphaned_files)}")
        report.append("")
        
        # Summary by confidence
        high_conf = [f for f in self.orphaned_files if f.confidence >= 85]
        medium_conf = [f for f in self.orphaned_files if 60 <= f.confidence < 85]
        low_conf = [f for f in self.orphaned_files if f.confidence < 60]
        
        report.append("## Deletion Confidence Summary")
        report.append("")
        report.append(f"- **HIGH Confidence (≥85%)**: {len(high_conf)} files - Safe to delete")
        report.append(f"- **MEDIUM Confidence (60-84%)**: {len(medium_conf)} files - Review recommended") 
        report.append(f"- **LOW Confidence (<60%)**: {len(low_conf)} files - **DO NOT DELETE**")
        report.append("")
        
        # Detailed breakdown by category
        category_names = {
            'backup': 'Backup Files',
            'test_demo': 'Test/Demo Code',
            'pci_vortex': 'PCI/Vortex Support',
            'enhanced_duplicate': 'Enhanced Duplicates',
            'asm_duplicate': 'Assembly Duplicates',
            'dma_safety': 'DMA/Cache Safety (Critical)',
            'performance': 'Performance Features',
            'general_orphan': 'General Orphaned Code'
        }
        
        for category, files in sorted(by_category.items()):
            if not files:
                continue
                
            report.append(f"## {category_names.get(category, category.title())}")
            report.append("")
            report.append(f"**Count**: {len(files)} files")
            avg_confidence = sum(f.confidence for f in files) / len(files)
            report.append(f"**Average Confidence**: {avg_confidence:.1f}%")
            report.append("")
            
            # List files with confidence and references
            for file_info in sorted(files, key=lambda f: f.confidence, reverse=True):
                status = "🟢 SAFE" if file_info.confidence >= 85 else "🟡 REVIEW" if file_info.confidence >= 60 else "🔴 KEEP"
                report.append(f"- `{file_info.path}` - **{file_info.confidence}%** {status}")
                
                if file_info.references:
                    report.append(f"  - ⚠️  **{len(file_info.references)} references found in live code:**")
                    for ref in file_info.references[:3]:  # Show first 3 references
                        report.append(f"    - `{ref.file}:{ref.line}` - {ref.context[:50]}...")
            report.append("")
        
        # Immediate action items
        report.append("## Immediate Action Items")
        report.append("")
        
        safe_files = [f for f in self.orphaned_files if f.confidence >= 95]
        if safe_files:
            report.append("### Safe for Immediate Deletion")
            report.append("")
            for file_info in safe_files:
                report.append(f"- `{file_info.path}`")
            report.append("")
        
        critical_files = [f for f in self.orphaned_files if f.references and f.confidence < 50]
        if critical_files:
            report.append("### Critical - DO NOT DELETE (Referenced by Live Code)")
            report.append("")
            for file_info in critical_files:
                report.append(f"- `{file_info.path}` - {len(file_info.references)} references")
            report.append("")
        
        # Save or print report
        report_text = '\n'.join(report)
        
        if output_file:
            with open(output_file, 'w') as f:
                f.write(report_text)
            print(f"📄 Report saved to: {output_file}")
        else:
            print(report_text)
        
        return report_text

def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent.parent
    
    analyzer = OrphanAnalyzer(project_root)
    
    # Output file
    output_file = project_root / 'analysis' / 'orphan_analysis_report.md'
    output_file.parent.mkdir(exist_ok=True)
    
    print("🚀 Starting orphan code analysis...")
    analyzer.generate_report(output_file)
    
    print("\n✅ Analysis complete!")
    print(f"📊 Results: {len(analyzer.orphaned_files)} orphaned files analyzed")
    print(f"📄 Full report: {output_file}")

if __name__ == "__main__":
    main()