const express = require('express');
const path = require('path');
const app = express();
const PORT = 3000;

// Serve static files from the 'public' directory
app.use(express.static(path.join(__dirname, 'public')));

// Main Route - Minimalist Black & White UI
app.get('/', (req, res) => {
    res.send(`
        <!DOCTYPE html>
        <html lang="en">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Download Center</title>
            <style>
                body {
                    background-color: #000000;
                    color: #ffffff;
                    font-family: monospace;
                    display: flex;
                    flex-direction: column;
                    align-items: center;
                    justify-content: center;
                    height: 100vh;
                    margin: 0;
                }
                h1 {
                    font-size: 2rem;
                    margin-bottom: 20px;
                    border-bottom: 2px solid #ffffff;
                    padding-bottom: 10px;
                }
                .download-btn {
                    color: #000000;
                    background-color: #ffffff;
                    padding: 10px 20px;
                    text-decoration: none;
                    font-weight: bold;
                    border: 2px solid #ffffff;
                    transition: all 0.3s ease;
                }
                .download-btn:hover {
                    color: #ffffff;
                    background-color: #000000;
                    cursor: pointer;
                }
                .info {
                    margin-top: 15px;
                    font-size: 0.8rem;
                    color: #888888;
                }
            </style>
        </head>
        <body>

            <h1>NIGGERS TESTING SITE</h1>
            <p>Click below to download the requested image file.</p>
            <br>
            <a href="/TheBook.iso" download class="download-btn">DOWNLOAD THEBOOK.ISO</a>
            <p class="info">Location: localhost:${PORT}/TheBook.iso</p>

        </body>
        </html>
    `);
});

// Start Server
app.listen(PORT, () => {
    console.log(`\n======================================`);
    console.log(` Server is running successfully!`);
    console.log(` Access URL: http://localhost:${PORT}`);
    console.log(`======================================\n`);
});
