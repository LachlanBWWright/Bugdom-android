# GitHub Pages Setup Guide

This repository is configured to automatically deploy the WebAssembly version of Bugdom to GitHub Pages.

## How it Works

The `.github/workflows/WebAssemblyBuild.yml` workflow automatically:

1. **Builds the WebAssembly version** using Emscripten
2. **Tests the build** in a headless browser using Playwright
3. **Deploys to GitHub Pages** (only on the `master` branch)
4. **Creates a release** with the WebAssembly artifacts

## Enabling GitHub Pages

To enable GitHub Pages for your fork:

1. Go to your repository settings on GitHub
2. Navigate to **Settings** → **Pages**
3. Under **Source**, select **GitHub Actions**
4. Save the changes

Once enabled, every push to the `master` branch will trigger a new deployment.

The site will be available at: `https://[your-username].github.io/[repo-name]/`

## What Gets Deployed

The GitHub Pages site includes:

- **Landing page** (`docs/index.html`) — Main page with game information and controls
- **Game files** — All WebAssembly files needed to run the game:
  - `Bugdom.html` → renamed to `game.html`
  - `Bugdom.js` — JavaScript glue code
  - `Bugdom.wasm` — WebAssembly binary
  - `Bugdom.data` — Game data packaged by Emscripten
- **Assets** — Screenshot and other resources

## Local Testing

To test the site locally before deploying:

1. Build the WebAssembly version:
   ```bash
   python3 build_wasm.py
   ```

2. Serve the files locally:
   ```bash
   python3 -m http.server 8000 --directory dist-wasm
   ```

3. Open http://localhost:8000/ in your browser

## Workflow Configuration

The workflow includes several important features:

### Permissions
```yaml
permissions:
  contents: write
  pages: write
  id-token: write
```

These permissions allow the workflow to:
- Write to the repository (for releases)
- Deploy to GitHub Pages
- Use OpenID Connect for secure authentication

### Concurrency Control
```yaml
concurrency:
  group: "pages-${{ github.ref }}"
  cancel-in-progress: true
```

This ensures only one deployment runs at a time per branch.

### Environment
```yaml
environment:
  name: github-pages
  url: ${{ steps.deployment.outputs.page_url }}
```

The workflow uses the `github-pages` environment, which provides protection rules and deployment history.

## Customization

### Updating the Landing Page

Edit `docs/index.html` to customize the landing page. Changes will be automatically deployed on the next push to `main`.

### Modifying Game Settings

The game supports URL parameters for customization:
- `?level=N` — Start at level N
- `?dev` — Enable developer tools
- `?noFenceCollision=1` — Disable fence collisions
- `?terrainFile=:Terrain:Custom.ter` — Load custom terrain

See the landing page for more details on the JavaScript API.

## Troubleshooting

### Build Fails
- Check that all submodules are initialized: `git submodule update --init --recursive`
- Verify Emscripten SDK version is compatible (latest is recommended)
- Check the workflow logs for specific error messages

### Deployment Doesn't Start
- Ensure you're pushing to the `master` branch
- Verify GitHub Pages is enabled in repository settings
- Check workflow permissions are correctly set

### Site Not Loading
- Wait a few minutes after deployment (can take 1-10 minutes to propagate)
- Check browser console for errors
- Ensure files are correctly uploaded to the `gh-pages` artifact
- Try clearing browser cache

## Manual Deployment

If you need to manually trigger a deployment:

1. Go to **Actions** tab in your repository
2. Select **WebAssembly Build and Deploy** workflow
3. Click **Run workflow**
4. Select the branch (should be `master`)
5. Click **Run workflow** button

## Related Files

- `.github/workflows/WebAssemblyBuild.yml` — Main workflow file
- `build_wasm.py` — WebAssembly build script
- `docs/index.html` — Landing page
- `dist-wasm/` — Output directory (generated, not in git)
- `BUILD.md` — Build instructions including WebAssembly
