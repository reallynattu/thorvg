#!/usr/bin/env node
/**
 * ThorVG Build Configurator Server
 * 
 * A simple HTTP server that:
 * 1. Serves the UI
 * 2. Runs meson builds with streaming output
 * 3. Reports build results and file sizes
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn, execSync } = require('child_process');

const PORT = process.env.PORT || 8080;
const THORVG_ROOT = path.resolve(__dirname, '../..');
const BUILD_DIR = path.join(THORVG_ROOT, 'build');

let lastBuildSuccess = false;
let lastBuildFiles = [];

const server = http.createServer(async (req, res) => {
  // CORS headers
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    res.end();
    return;
  }

  const url = new URL(req.url, `http://localhost:${PORT}`);

  // Serve static files
  if (req.method === 'GET' && (url.pathname === '/' || url.pathname === '/index.html')) {
    const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf-8');
    res.writeHead(200, { 'Content-Type': 'text/html' });
    res.end(html);
    return;
  }

  // Build status endpoint
  if (req.method === 'GET' && url.pathname === '/api/build/status') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ 
      success: lastBuildSuccess, 
      files: lastBuildFiles 
    }));
    return;
  }

  // Build endpoint
  if (req.method === 'POST' && url.pathname === '/api/build') {
    let body = '';
    req.on('data', chunk => body += chunk);
    req.on('end', async () => {
      try {
        const { command } = JSON.parse(body);
        
        res.writeHead(200, {
          'Content-Type': 'text/plain',
          'Transfer-Encoding': 'chunked',
          'Cache-Control': 'no-cache'
        });

        lastBuildSuccess = false;
        lastBuildFiles = [];

        // Clean previous build
        res.write('🗑️  Cleaning previous build...\n');
        try {
          execSync('rm -rf build', { cwd: THORVG_ROOT });
        } catch (e) {
          // Ignore if build dir doesn't exist
        }

        // Extract meson command and add --wipe if needed
        const mesonCmd = command.replace('meson setup build', 'meson setup build --wipe');
        
        res.write(`\n📦 Running: ${mesonCmd}\n\n`);

        // Run meson setup
        const setup = spawn('sh', ['-c', mesonCmd], { 
          cwd: THORVG_ROOT,
          env: { ...process.env, TERM: 'dumb' }
        });

        setup.stdout.on('data', data => res.write(data));
        setup.stderr.on('data', data => res.write(data));

        await new Promise((resolve, reject) => {
          setup.on('close', code => {
            if (code !== 0) {
              res.write(`\n❌ Meson setup failed with code ${code}\n`);
              reject(new Error('Meson setup failed'));
            } else {
              res.write('\n✅ Meson setup complete\n\n');
              resolve();
            }
          });
        });

        // Run ninja build
        res.write('🔨 Building with ninja...\n\n');
        
        const ninja = spawn('ninja', ['-C', 'build'], { 
          cwd: THORVG_ROOT,
          env: { ...process.env, TERM: 'dumb' }
        });

        ninja.stdout.on('data', data => res.write(data));
        ninja.stderr.on('data', data => res.write(data));

        await new Promise((resolve, reject) => {
          ninja.on('close', code => {
            if (code !== 0) {
              res.write(`\n❌ Build failed with code ${code}\n`);
              reject(new Error('Build failed'));
            } else {
              res.write('\n✅ Build complete!\n');
              resolve();
            }
          });
        });

        // Get output file sizes
        res.write('\n📊 Checking output files...\n');
        
        const libDir = path.join(BUILD_DIR, 'src');
        if (fs.existsSync(libDir)) {
          const files = fs.readdirSync(libDir)
            .filter(f => f.endsWith('.dylib') || f.endsWith('.so') || f.endsWith('.a') || f.endsWith('.dll'));
          
          for (const file of files) {
            const filePath = path.join(libDir, file);
            const stats = fs.statSync(filePath);
            const sizeKB = (stats.size / 1024).toFixed(1);
            const sizeMB = (stats.size / (1024 * 1024)).toFixed(2);
            
            const sizeStr = stats.size > 1024 * 1024 
              ? `${sizeMB} MB` 
              : `${sizeKB} KB`;
            
            lastBuildFiles.push({ name: file, size: sizeStr });
            res.write(`   📁 ${file}: ${sizeStr}\n`);
          }
        }

        // Check for tools
        const toolsDir = path.join(BUILD_DIR, 'tools');
        if (fs.existsSync(toolsDir)) {
          const checkTool = (name) => {
            const toolPath = path.join(toolsDir, name, `tvg-${name}`);
            if (fs.existsSync(toolPath)) {
              const stats = fs.statSync(toolPath);
              const sizeKB = (stats.size / 1024).toFixed(1);
              lastBuildFiles.push({ name: `tvg-${name}`, size: `${sizeKB} KB` });
              res.write(`   🔧 tvg-${name}: ${sizeKB} KB\n`);
            }
          };
          checkTool('svg2png');
          checkTool('lottie2gif');
        }

        lastBuildSuccess = true;
        res.write('\n🎉 All done!\n');
        res.end();

      } catch (err) {
        lastBuildSuccess = false;
        res.write(`\n❌ Error: ${err.message}\n`);
        res.end();
      }
    });
    return;
  }

  // 404
  res.writeHead(404, { 'Content-Type': 'text/plain' });
  res.end('Not found');
});

server.listen(PORT, () => {
  console.log(`
╔═══════════════════════════════════════════════════════════╗
║                                                           ║
║   ⚡ ThorVG Build Configurator                            ║
║                                                           ║
║   Server running at: http://localhost:${PORT}               ║
║   ThorVG root: ${THORVG_ROOT.slice(-40).padStart(40)}   ║
║                                                           ║
║   Open your browser to configure and build ThorVG        ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝
  `);
});
