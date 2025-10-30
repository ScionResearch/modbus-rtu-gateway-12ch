#!/usr/bin/env python3
"""
PlatformIO Build Script for Web Asset Minification
Minifies JS and CSS files from /web folder and copies to /data folder
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

Import("env")

def check_terser_available():
    """Check if Terser is available globally"""
    import shutil
    
    # First check if terser is in PATH
    terser_path = shutil.which("terser")
    if terser_path:
        print(f"✓ Found Terser at: {terser_path}")
        try:
            # Test if it actually works
            result = subprocess.run([terser_path, "--version"], 
                                  capture_output=True, text=True, timeout=5)
            if result.returncode == 0:
                print(f"✓ Terser version: {result.stdout.strip()}")
                return True
            else:
                print(f"✗ Terser failed with exit code {result.returncode}")
                print(f"  stderr: {result.stderr}")
        except Exception as e:
            print(f"✗ Error running Terser: {e}")
    else:
        print("✗ Terser not found in PATH")
        # Print PATH for debugging
        import os
        print(f"Current PATH: {os.environ.get('PATH', 'Not set')}")
    
    print("Terser not available - using Python minification")
    return False

def minify_js_python(input_file, output_file):
    """Safe JS minification - focuses on reliable compression without breaking syntax"""
    with open(input_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    import re
    
    # Step 1: Remove safe comments only
    # Remove single-line comments at start of line
    content = re.sub(r'^\s*//.*$', '', content, flags=re.MULTILINE)
    
    # Remove multi-line comments (but preserve license comments)
    content = re.sub(r'/\*(?!\!).*?\*/', '', content, flags=re.DOTALL)
    
    # Step 2: Safe whitespace reduction
    # Remove leading/trailing whitespace from lines
    content = re.sub(r'^\s+', '', content, flags=re.MULTILINE)
    content = re.sub(r'\s+$', '', content, flags=re.MULTILINE)
    
    # Remove empty lines
    content = re.sub(r'\n\s*\n', '\n', content)
    
    # Step 3: Very conservative operator spacing (only safe cases)
    # Remove spaces around semicolons and commas (but not in strings)
    content = re.sub(r'\s*;\s*', ';', content)
    content = re.sub(r'\s*,\s*', ',', content)
    
    # Step 4: Safe function/control structure compression
    content = re.sub(r'function\s*\(', 'function(', content)
    content = re.sub(r'if\s*\(', 'if(', content)
    content = re.sub(r'for\s*\(', 'for(', content)
    content = re.sub(r'while\s*\(', 'while(', content)
    
    # Step 5: Remove unnecessary semicolons before closing braces
    content = re.sub(r';\s*}', '}', content)
    
    # Step 6: Final cleanup
    content = re.sub(r'\n+', '\n', content)
    
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(content.strip())

def minify_css_python(input_file, output_file):
    """Basic CSS minification using Python (fallback)"""
    with open(input_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    import re
    
    # Remove comments
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    
    # Remove extra whitespace
    content = re.sub(r'\s+', ' ', content)
    content = re.sub(r';\s*}', '}', content)
    content = re.sub(r'{\s*', '{', content)
    content = re.sub(r'}\s*', '}', content)
    content = re.sub(r';\s*', ';', content)
    content = re.sub(r':\s*', ':', content)
    
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(content.strip())

def minify_files(target=None, source=None, env=None):
    """Main minification function"""
    # Use the global env if not passed as parameter
    if env is None:
        from SCons.Script import DefaultEnvironment
        env = DefaultEnvironment()
    
    project_dir = Path(env.get("PROJECT_DIR"))
    web_dir = project_dir / "web"
    data_dir = project_dir / "data"
    
    print(f"Minifying web assets...")
    print(f"Source: {web_dir}")
    print(f"Target: {data_dir}")
    
    if not web_dir.exists():
        print(f"Warning: Web source directory {web_dir} does not exist")
        return
    
    # Ensure data directories exist
    (data_dir / "script").mkdir(parents=True, exist_ok=True)
    (data_dir / "style").mkdir(parents=True, exist_ok=True)
    (data_dir / "images").mkdir(parents=True, exist_ok=True)
    (data_dir / "fonts").mkdir(parents=True, exist_ok=True)
    
    # Check if Terser is available without npm installation
    use_terser = check_terser_available()
    
    # Process JavaScript files
    js_files = list(web_dir.glob("script/*.js"))
    for js_file in js_files:
        output_file = data_dir / "script" / js_file.name
        
        # Skip minification for already minified files (like sortable.min.js)
        if js_file.name.endswith('.min.js'):
            print(f"Copying pre-minified JS: {js_file.name}")
            shutil.copy2(js_file, output_file)
            continue
            
        print(f"Minifying JS: {js_file.name}")
        
        # Try Terser minification first, fallback to safe Python minification
        if use_terser:
            try:
                # Use shutil.which to get the full path to terser
                terser_path = shutil.which("terser")
                if not terser_path:
                    raise FileNotFoundError("Terser not found in PATH")
                
                # Use the exact command structure that works manually: terser script.js -o output.js -c -m
                subprocess.run([
                    terser_path, str(js_file),
                    "-o", str(output_file),
                    "-c", "-m"
                ], check=True, timeout=30)
                print(f"✓ Minified with Terser: {js_file.name}")
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError) as e:
                print(f"Terser failed for {js_file.name}: {e}, using safe Python fallback")
                minify_js_python(js_file, output_file)
        else:
            print(f"Using safe Python minification: {js_file.name}")
            minify_js_python(js_file, output_file)
    
    # Process CSS files
    css_files = list(web_dir.glob("style/*.css"))
    for css_file in css_files:
        output_file = data_dir / "style" / css_file.name
        print(f"Minifying CSS: {css_file.name}")
        
        # Always use Python CSS minification for reliability
        minify_css_python(css_file, output_file)
    
    # Copy HTML files (no minification needed for this project)
    html_files = list(web_dir.glob("*.html"))
    for html_file in html_files:
        output_file = data_dir / html_file.name
        print(f"Copying HTML: {html_file.name}")
        shutil.copy2(html_file, output_file)
    
    # Copy image files
    if (web_dir / "images").exists():
        for img_file in (web_dir / "images").iterdir():
            if img_file.is_file():
                output_file = data_dir / "images" / img_file.name
                print(f"Copying image: {img_file.name}")
                shutil.copy2(img_file, output_file)
    
    # Copy font files (Font Awesome and other fonts)
    if (web_dir / "fonts").exists():
        for font_file in (web_dir / "fonts").iterdir():
            if font_file.is_file():
                output_file = data_dir / "fonts" / font_file.name
                print(f"Copying font: {font_file.name}")
                shutil.copy2(font_file, output_file)
    
    print("Web asset minification completed!")
    
    # Clean up package files
    for file in ["package.json", "package-lock.json"]:
        if os.path.exists(file):
            os.remove(file)
    if os.path.exists("node_modules"):
        shutil.rmtree("node_modules")

def build_filesystem(target=None, source=None, env=None):
    """Build the filesystem image after minification"""
    print("Building filesystem image...")
    try:
        # Run the filesystem build command
        subprocess.run([
            env.get("PYTHONEXE"), "-m", "platformio", "run",
            "--target", "buildfs", "--environment", env.get("PIOENV")
        ], check=True)
        print("Filesystem image built successfully!")
    except subprocess.CalledProcessError as e:
        print(f"Filesystem build failed: {e}")

def upload_filesystem(target=None, source=None, env=None):
    """Upload the filesystem image to device"""
    print("Uploading filesystem image...")
    try:
        # Run the filesystem upload command
        subprocess.run([
            env.get("PYTHONEXE"), "-m", "platformio", "run",
            "--target", "uploadfs", "--environment", env.get("PIOENV")
        ], check=True)
        print("Filesystem uploaded successfully!")
    except subprocess.CalledProcessError as e:
        print(f"Filesystem upload failed: {e}")

# Register the minification as a pre-build action
def minify_and_build(target=None, source=None, env=None):
    """Combined minification and filesystem build"""
    minify_files(target, source, env)
    build_filesystem(target, source, env)

def minify_build_and_upload(target=None, source=None, env=None):
    """Combined minification, filesystem build, and upload"""
    minify_files(target, source, env)
    build_filesystem(target, source, env)
    upload_filesystem(target, source, env)

# Add custom target for manual minification
env.AddCustomTarget(
    name="minify",
    dependencies=None,
    actions=[
        minify_files
    ],
    title="Minify Web Assets",
    description="Minify JS and CSS files from /web to /data"
)

# Add custom target for minify + filesystem build
env.AddCustomTarget(
    name="minify-fs",
    dependencies=None,
    actions=[
        minify_and_build
    ],
    title="Minify and Build Filesystem",
    description="Minify web assets and build filesystem image"
)

# Add custom target for minify + build + upload filesystem
env.AddCustomTarget(
    name="minify-deploy",
    dependencies=None,
    actions=[
        minify_build_and_upload
    ],
    title="Minify, Build and Upload Filesystem",
    description="Minify web assets, build filesystem image, and upload to device"
)

print("Web minification script loaded. Available commands:")
print("  pio run -t minify        # Minify web assets only")
print("  pio run -t minify-fs     # Minify assets and build filesystem")
print("  pio run -t minify-deploy # Minify, build filesystem, and upload to device")
