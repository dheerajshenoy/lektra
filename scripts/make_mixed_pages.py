from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.units import mm

# --- Helper function to draw something on the page for demonstration ---
def draw_page_label(c, text):
    """Draws a simple label in the center of the current page."""
    width, height = c._pagesize
    c.setFont("Helvetica", 16)
    # Center the text
    text_width = c.stringWidth(text, "Helvetica", 16)
    c.drawString((width - text_width) / 2, height / 2, text)

# --- 1. Create the canvas (initial size is temporary) ---
# The initial pagesize here doesn't matter much as we will override it for every page.
c = canvas.Canvas("mixed_dimensions_reportlab.pdf", pagesize=A4)

# --- 2. Page 1: Portrait, Standard A4 ---
# Set the page size (default is portrait if you just pass A4)
c.setPageSize(A4)
draw_page_label(c, "Page 1: A4 Portrait")
c.showPage() # Saves the current page and starts a new one

# --- 3. Page 2: Landscape, Standard A4 ---
# Use the landscape() helper function from reportlab.lib.pagesizes
c.setPageSize(landscape(A4))
draw_page_label(c, "Page 2: A4 Landscape")
c.showPage()

# --- 4. Page 3: Portrait, Custom Size (100mm x 150mm) ---
# Custom sizes are defined as tuples (width, height) in points.
# Multiply by unit constants (like mm) from reportlab.lib.units for convenience.
c.setPageSize((100*mm, 150*mm))
draw_page_label(c, "Page 3: Custom Portrait")
c.showPage()

# --- 5. Page 4: Landscape, Custom Size (200mm x 100mm) ---
# For a custom landscape size, you just swap the width and height values.
c.setPageSize((200*mm, 100*mm))
draw_page_label(c, "Page 4: Custom Landscape")
c.showPage()

# --- 6. Save the PDF ---
c.save()

print("PDF 'mixed_dimensions_reportlab.pdf' generated successfully!")
