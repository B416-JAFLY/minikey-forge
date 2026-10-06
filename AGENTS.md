# Project instructions

Use PowerShell 7 on Windows and project-local dependencies. Do not install packages globally.

Commit important, coherent changes after their relevant checks pass. Describe behavior and validation in commit messages. Do not automatically push future commits unless the user requests publication or sync.

Never commit `.env`, `private/`, `build/`, hardware credential logs, key-bearing firmware binaries or archives. `.env.example` must contain no working key. Never regenerate or rotate an existing device master key during an ordinary update. Host tests use a separate public test key.

Preserve existing credentials during upgrades. Do not reset the authenticator, change chip protection options, or erase data outside the documented storage area as a routine fix.

Record hardware verification separately from host simulation. A compiled image or passing host tests does not prove hardware acceptance.
