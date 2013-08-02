// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_PRINTING_PRINT_WEB_VIEW_HELPER_H_
#define CHROME_RENDERER_PRINTING_PRINT_WEB_VIEW_HELPER_H_

#include <vector>

#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/shared_memory.h"
#include "base/time.h"
#include "content/public/renderer/render_view_observer.h"
#include "content/public/renderer/render_view_observer_tracker.h"
#include "printing/metafile_impl.h"
#include "third_party/WebKit/public/platform/WebCanvas.h"
#include "third_party/WebKit/public/web/WebNode.h"
#include "third_party/WebKit/public/web/WebPrintParams.h"
#include "ui/gfx/size.h"

struct PrintMsg_Print_Params;
struct PrintMsg_PrintPage_Params;
struct PrintMsg_PrintPages_Params;

namespace base {
class DictionaryValue;
}

namespace WebKit {
class WebFrame;
class WebView;
}

namespace printing {

struct PageSizeMargins;
class PrepareFrameAndViewForPrint;

// PrintWebViewHelper handles most of the printing grunt work for RenderView.
// We plan on making print asynchronous and that will require copying the DOM
// of the document and creating a new WebView with the contents.
class PrintWebViewHelper
    : public content::RenderViewObserver,
      public content::RenderViewObserverTracker<PrintWebViewHelper> {
 public:
  explicit PrintWebViewHelper(content::RenderView* render_view);
  virtual ~PrintWebViewHelper();

  void PrintNode(const WebKit::WebNode& node);

 private:
  friend class PrintWebViewHelperTestBase;
  FRIEND_TEST_ALL_PREFIXES(PrintWebViewHelperTest,
                           BlockScriptInitiatedPrinting);
  FRIEND_TEST_ALL_PREFIXES(PrintWebViewHelperTest,
                           BlockScriptInitiatedPrintingFromPopup);
  FRIEND_TEST_ALL_PREFIXES(PrintWebViewHelperTest, OnPrintPages);

#if defined(OS_WIN) || defined(OS_MACOSX)
  FRIEND_TEST_ALL_PREFIXES(PrintWebViewHelperTest, PrintLayoutTest);
  FRIEND_TEST_ALL_PREFIXES(PrintWebViewHelperTest, PrintWithIframe);
#endif  // defined(OS_WIN) || defined(OS_MACOSX)

  enum PrintingResult {
    OK,
    FAIL_PRINT_INIT,
    FAIL_PRINT,
    FAIL_PREVIEW,
  };

  enum PrintPreviewErrorBuckets {
    PREVIEW_ERROR_NONE,  // Always first.
    PREVIEW_ERROR_BAD_SETTING,
    PREVIEW_ERROR_METAFILE_COPY_FAILED,
    PREVIEW_ERROR_METAFILE_INIT_FAILED,
    PREVIEW_ERROR_ZERO_PAGES,
    PREVIEW_ERROR_MAC_DRAFT_METAFILE_INIT_FAILED,
    PREVIEW_ERROR_PAGE_RENDERED_WITHOUT_METAFILE,
    PREVIEW_ERROR_UPDATING_PRINT_SETTINGS,
    PREVIEW_ERROR_INVALID_PRINTER_SETTINGS,
    PREVIEW_ERROR_LAST_ENUM  // Always last.
  };

  enum PrintPreviewRequestType {
    PRINT_PREVIEW_USER_INITIATED_ENTIRE_FRAME,
    PRINT_PREVIEW_USER_INITIATED_SELECTION,
    PRINT_PREVIEW_USER_INITIATED_CONTEXT_NODE,
    PRINT_PREVIEW_SCRIPTED  // triggered by window.print().
  };

  // RenderViewObserver implementation.
  virtual bool OnMessageReceived(const IPC::Message& message) OVERRIDE;
  virtual void PrintPage(WebKit::WebFrame* frame, bool user_initiated) OVERRIDE;

  // Message handlers ---------------------------------------------------------

  // Print the document.
  void OnPrintPages();

  // Print the document with the print preview frame/node.
  void OnPrintForSystemDialog();

  // Get |page_size| and |content_area| information from
  // |page_layout_in_points|.
  void GetPageSizeAndContentAreaFromPageLayout(
      const PageSizeMargins& page_layout_in_points,
      gfx::Size* page_size,
      gfx::Rect* content_area);

  // Update |ignore_css_margins_| based on settings.
  void UpdateFrameMarginsCssInfo(const base::DictionaryValue& settings);

  // Returns true if the current destination printer is PRINT_TO_PDF.
  bool IsPrintToPdfRequested(const base::DictionaryValue& settings);

  // Returns the print scaling option to retain/scale/crop the source page size
  // to fit the printable area of the paper.
  //
  // We retain the source page size when the current destination printer is
  // SAVE_AS_PDF.
  //
  // We crop the source page size to fit the printable area or we print only the
  // left top page contents when
  // (1) Source is PDF and the user has requested not to fit to printable area
  // via |job_settings|.
  // (2) Source is PDF. This is the first preview request and print scaling
  // option is disabled for initiator renderer plugin.
  //
  // In all other cases, we scale the source page to fit the printable area.
  WebKit::WebPrintScalingOption GetPrintScalingOption(
      bool source_is_html,
      const base::DictionaryValue& job_settings,
      const PrintMsg_Print_Params& params);

  // Initiate print preview.
  void OnInitiatePrintPreview(bool selection_only);

  // Start the process of generating a print preview using |settings|.
  void OnPrintPreview(const base::DictionaryValue& settings);

  // Prepare frame for creating preview document.
  void PrepareFrameForPreviewDocument();

  // Continue creating preview document.
  void OnFramePreparedForPreviewDocument();

  // Initialize the print preview document.
  bool CreatePreviewDocument();

  // Renders a print preview page. |page_number| is 0-based.
  // Returns true if print preview should continue, false on failure.
  bool RenderPreviewPage(int page_number,
                         const PrintMsg_Print_Params& print_params);

  // Finalize the print ready preview document.
  bool FinalizePrintReadyDocument();

  // Print / preview the node under the context menu.
  void OnPrintNodeUnderContextMenu();

  // Print the pages for print preview. Do not display the native print dialog
  // for user settings. |job_settings| has new print job settings values.
  void OnPrintForPrintPreview(const base::DictionaryValue& job_settings);

  void OnPrintingDone(bool success);

  // Enable/Disable window.print calls.  If |blocked| is true window.print
  // calls will silently fail.  Call with |blocked| set to false to reenable.
  void SetScriptedPrintBlocked(bool blocked);

  // Main printing code -------------------------------------------------------

  void Print(WebKit::WebFrame* frame, const WebKit::WebNode& node);

  // Notification when printing is done - signal tear-down/free resources.
  void DidFinishPrinting(PrintingResult result);

  // Print Settings -----------------------------------------------------------

  // Initialize print page settings with default settings.
  // Used only for native printing workflow.
  bool InitPrintSettings(bool fit_to_paper_size);

  // Calculate number of pages in source document.
  bool CalculateNumberOfPages(WebKit::WebFrame* frame,
                              const WebKit::WebNode& node,
                              int* number_of_pages);

  // Update the current print settings with new |passed_job_settings|.
  // |passed_job_settings| dictionary contains print job details such as printer
  // name, number of copies, page range, etc.
  bool UpdatePrintSettings(WebKit::WebFrame* frame,
                           const WebKit::WebNode& node,
                           const base::DictionaryValue& passed_job_settings);

  // Get final print settings from the user.
  // Return false if the user cancels or on error.
  bool GetPrintSettingsFromUser(WebKit::WebFrame* frame,
                                const WebKit::WebNode& node,
                                int expected_pages_count);

  // Page Printing / Rendering ------------------------------------------------

  void OnFramePreparedForPrintPages();
  void PrintPages();
  bool PrintPagesNative(WebKit::WebFrame* frame,
                        const WebKit::WebNode& node,
                        int page_count,
                        const gfx::Size& canvas_size);
  void FinishFramePrinting();

  // Prints the page listed in |params|.
#if defined(USE_X11)
  void PrintPageInternal(const PrintMsg_PrintPage_Params& params,
                         const gfx::Size& canvas_size,
                         WebKit::WebFrame* frame,
                         Metafile* metafile);
#else
  void PrintPageInternal(const PrintMsg_PrintPage_Params& params,
                         const gfx::Size& canvas_size,
                         WebKit::WebFrame* frame);
#endif

  // Render the frame for printing.
  bool RenderPagesForPrint(WebKit::WebFrame* frame,
                           const WebKit::WebNode& node);

  // Platform specific helper function for rendering page(s) to |metafile|.
#if defined(OS_WIN)
  void RenderPage(const PrintMsg_Print_Params& params,
                  int page_number,
                  WebKit::WebFrame* frame,
                  bool is_preview,
                  Metafile* metafile,
                  double* scale_factor,
                  gfx::Size* page_size_in_dpi,
                  gfx::Rect* content_area_in_dpi);
#elif defined(OS_MACOSX)
  void RenderPage(const PrintMsg_Print_Params& params,
                  int page_number,
                  WebKit::WebFrame* frame,
                  bool is_preview,
                  Metafile* metafile,
                  gfx::Size* page_size,
                  gfx::Rect* content_rect);
#endif  // defined(OS_WIN)

  // Renders page contents from |frame| to |content_area| of |canvas|.
  // |page_number| is zero-based.
  // When method is called, canvas should be setup to draw to |canvas_area|
  // with |scale_factor|.
  static float RenderPageContent(WebKit::WebFrame* frame,
                                 int page_number,
                                 const gfx::Rect& canvas_area,
                                 const gfx::Rect& content_area,
                                 double scale_factor,
                                 WebKit::WebCanvas* canvas);

  // Helper methods -----------------------------------------------------------

  bool CopyMetafileDataToSharedMem(Metafile* metafile,
                                   base::SharedMemoryHandle* shared_mem_handle);

  // Helper method to get page layout in points and fit to page if needed.
  static void ComputePageLayoutInPointsForCss(
      WebKit::WebFrame* frame,
      int page_index,
      const PrintMsg_Print_Params& default_params,
      bool ignore_css_margins,
      double* scale_factor,
      PageSizeMargins* page_layout_in_points);

  // Given the |device| and |canvas| to draw on, prints the appropriate headers
  // and footers using strings from |header_footer_info| on to the canvas.
  static void PrintHeaderAndFooter(
      WebKit::WebCanvas* canvas,
      int page_number,
      int total_pages,
      float webkit_scale_factor,
      const PageSizeMargins& page_layout_in_points,
      const base::DictionaryValue& header_footer_info,
      const PrintMsg_Print_Params& params);

  bool GetPrintFrame(WebKit::WebFrame** frame);

  // This reports the current time - |start_time| as the time to render a page.
  void ReportPreviewPageRenderTime(base::TimeTicks start_time);

  // Script Initiated Printing ------------------------------------------------

  // Return true if script initiated printing is currently
  // allowed. |user_initiated| should be true when a user event triggered the
  // script, most likely by pressing a print button on the page.
  bool IsScriptInitiatedPrintAllowed(WebKit::WebFrame* frame,
                                     bool user_initiated);

  // Returns true if script initiated printing occurs too often.
  bool IsScriptInitiatedPrintTooFrequent(WebKit::WebFrame* frame);

  // Reset the counter for script initiated printing.
  // Scripted printing will be allowed to continue.
  void ResetScriptedPrintCount();

  // Increment the counter for script initiated printing.
  // Scripted printing will be blocked for a limited amount of time.
  void IncrementScriptedPrintCount();

  void RequestPrintPreview(PrintPreviewRequestType type);

  // Checks whether print preview should continue or not.
  // Returns true if cancelling, false if continuing.
  bool CheckForCancel();

  // Notifies the browser a print preview page has been rendered.
  // |page_number| is 0-based.
  // For a valid |page_number| with modifiable content,
  // |metafile| is the rendered page. Otherwise |metafile| is NULL.
  // Returns true if print preview should continue, false on failure.
  bool PreviewPageRendered(int page_number, Metafile* metafile);

  // WebView used only to print the selection.
  scoped_ptr<PrepareFrameAndViewForPrint> prep_frame_view_;
  bool reset_prep_frame_view_;

  scoped_ptr<PrintMsg_PrintPages_Params> print_pages_params_;
  bool is_preview_enabled_;
  bool is_scripted_print_throttling_disabled_;
  bool is_print_ready_metafile_sent_;
  bool ignore_css_margins_;

  // Used for scripted initiated printing blocking.
  base::Time last_cancelled_script_print_;
  int user_cancelled_scripted_print_count_;
  bool is_scripted_printing_blocked_;

  // Let the browser process know of a printing failure. Only set to false when
  // the failure came from the browser in the first place.
  bool notify_browser_of_print_failure_;

  // True, when printing from print preview.
  bool print_for_preview_;

  scoped_ptr<PrintMsg_PrintPages_Params> old_print_pages_params_;

  // Strings generated by the browser process to be printed as headers and
  // footers if requested by the user.
  scoped_ptr<base::DictionaryValue> header_footer_info_;

  // Keeps track of the state of print preview between messages.
  // TODO(vitalybuka): Create PrintPreviewContext when needed and delete after
  // use. Now it's interaction with various messages is confusing.
  class PrintPreviewContext {
   public:
    PrintPreviewContext();
    ~PrintPreviewContext();

    // Initializes the print preview context. Need to be called to set
    // the |web_frame| / |web_node| to generate the print preview for.
    void InitWithFrame(WebKit::WebFrame* web_frame);
    void InitWithNode(const WebKit::WebNode& web_node);

    // Does bookkeeping at the beginning of print preview.
    void OnPrintPreview();

    // Create the print preview document. |pages| is empty to print all pages.
    // Takes ownership of |prepared_frame|.
    bool CreatePreviewDocument(PrepareFrameAndViewForPrint* prepared_frame,
                               const std::vector<int>& pages);

    // Called after a page gets rendered. |page_time| is how long the
    // rendering took.
    void RenderedPreviewPage(const base::TimeDelta& page_time);

    // Updates the print preview context when the required pages are rendered.
    void AllPagesRendered();

    // Finalizes the print ready preview document.
    void FinalizePrintReadyDocument();

    // Cleanup after print preview finishes.
    void Finished();

    // Cleanup after print preview fails.
    void Failed(bool report_error);

    // Helper functions
    int GetNextPageNumber();
    bool IsRendering() const;
    bool IsModifiable() const;
    bool HasSelection() const;
    bool IsLastPageOfPrintReadyMetafile() const;
    bool IsFinalPageRendered() const;

    // Setters
    void set_generate_draft_pages(bool generate_draft_pages);
    void set_error(enum PrintPreviewErrorBuckets error);

    // Getters
    // Original frame for which preview was requested.
    WebKit::WebFrame* source_frame();
    // Original node for which preview was requested.
    const WebKit::WebNode& source_node() const;

    // Frame to be use to render preview. May be the same as source_frame(), or
    // generated from it, e.g. copy of selected block.
    WebKit::WebFrame* prepared_frame();
    // Node to be use to render preview. May be the same as source_node(), or
    // generated from it, e.g. copy of selected block.
    const WebKit::WebNode& prepared_node() const;

    int total_page_count() const;
    bool generate_draft_pages() const;
    PreviewMetafile* metafile();
    gfx::Size GetPrintCanvasSize() const;
    int last_error() const;

   private:
    enum State {
      UNINITIALIZED,  // Not ready to render.
      INITIALIZED,    // Ready to render.
      RENDERING,      // Rendering.
      DONE            // Finished rendering.
    };

    // Reset some of the internal rendering context.
    void ClearContext();

    // Specifies what to render for print preview.
    WebKit::WebFrame* source_frame_;
    WebKit::WebNode source_node_;

    scoped_ptr<PrepareFrameAndViewForPrint> prep_frame_view_;
    scoped_ptr<PreviewMetafile> metafile_;

    // Total page count in the renderer.
    int total_page_count_;

    // The current page to render.
    int current_page_index_;

    // List of page indices that need to be rendered.
    std::vector<int> pages_to_render_;

    // True, when draft pages needs to be generated.
    bool generate_draft_pages_;

    // Specifies the total number of pages in the print ready metafile.
    int print_ready_metafile_page_count_;

    base::TimeDelta document_render_time_;
    base::TimeTicks begin_time_;

    enum PrintPreviewErrorBuckets error_;

    State state_;
  };

  bool print_node_in_progress_;
  PrintPreviewContext print_preview_context_;
  DISALLOW_COPY_AND_ASSIGN(PrintWebViewHelper);
};

}  // namespace printing

#endif  // CHROME_RENDERER_PRINTING_PRINT_WEB_VIEW_HELPER_H_
