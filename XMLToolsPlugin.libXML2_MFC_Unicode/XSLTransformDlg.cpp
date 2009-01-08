// XSLTransformDlg.cpp : implementation file
//

#include "stdafx.h"
#include "Scintilla.h"
#include "XMLTools.h"
#include "XSLTransformDlg.h"
#include "Report.h"
#include "menuCmdID.h"
#include "LoadLibrary.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CXSLTransformDlg dialog

CXSLTransformDlg::CXSLTransformDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CXSLTransformDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CXSLTransformDlg)
	m_sXSLTFile = _T("");
	m_sXSLTOptions = _T("");
	//}}AFX_DATA_INIT
}


void CXSLTransformDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CXSLTransformDlg)
	DDX_Text(pDX, IDC_EDIT_XSLTFILE, m_sXSLTFile);
	DDX_Text(pDX, IDC_EDIT_XSLTOPTIONS, m_sXSLTOptions);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CXSLTransformDlg, CDialog)
	//{{AFX_MSG_MAP(CXSLTransformDlg)
	ON_BN_CLICKED(IDC_BTN_TRANSFORM, OnBtnTransform)
	ON_BN_CLICKED(IDC_BTN_BROWSEXSLTFILE, OnBtnBrowseXSLTFile)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CXSLTransformDlg message handlers

HWND CXSLTransformDlg::getCurrentHScintilla(int which)
{
  return (which == 0)?nppData._scintillaMainHandle:nppData._scintillaSecondHandle;
}

BOOL CXSLTransformDlg::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
  CRect myRect;
  GetWindowRect(&myRect);

  MoveWindow(myRect.left+100, myRect.top+100, myRect.Width(), myRect.Height());

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

/*
  Recherche la paire cl�/valeur suivante dans la string, en partant de la
  position indiqu�e. La fonction retourne la position � laquelle le parsing
  s'est arr�t� ou -1 si aucune paire n'a �t� trouv�e.
  De plus, elle remplit les variable key et value avec respectivement la
  cl� et la valeur lues.
*/
int getNextParam(std::wstring str, int startpos, std::wstring *key, std::wstring *val) {
  int len = str.length();
  if (startpos < 0 || startpos >= len || !key || !val) return -1;
  
  // on saute les espaces, les tabs et les retours � la ligne
  int keypos = str.find_first_not_of(L" \t\r\n", startpos);
  if (keypos < 0 || keypos >= len) return -1;
  
  // le caract�re suivant ne doit pas �tre un '='
  if (str.at(keypos) == '=') return -1;
  
  // keypos d�signe le d�but de notre key, il faut trouver le '=' (ou le ' ')
  int valpos = str.find_first_of(L"=", keypos+1);
  valpos = str.find_last_not_of(L" =", valpos);  // on recherche la derni�re lettre de la cl�
  *key = str.substr(keypos, valpos-keypos+1);

  // on saute le '='
  valpos = 1+str.find_first_of(L"=", valpos+1);

  if (str.at(valpos) == ' ') valpos = str.find_first_not_of(L" ", valpos);  // on saute les �ventuels espaces
  if (valpos < 0 || valpos >= len) return -1;
  
  // ici, il faut parser la string; si elle commence par un ', on recherche un autre ',
  // sinon on ne prend que le 1e mot que l'on trouve
  int valendpos = valpos;
  if (str.at(valendpos) == '\'') {
    valendpos = str.find_first_of(L"\'", valendpos+1);
    *val = str.substr(valpos, valendpos-valpos+1);
  } else {
    valendpos = str.find_first_of(L" \t\r\n", valendpos);
    // si on est en bout de string, valendpos vaudra -1
    if (valendpos < 0) valendpos = len;
    *val = str.substr(valpos, valendpos-valpos);
  }
  
  return valendpos;
}

void cleanParams(char* params[], int nparams) {
  for (int i = 0; i < nparams; ++i) {
    if (params[i]) delete[] params[i];
  }
}

void CXSLTransformDlg::OnBtnTransform() {
  #define MAX_PARAMS 16
  this->UpdateData();

  // proceed to transformation
  int i;
  char *params[MAX_PARAMS + 1];
  int nbparams = 0;
  xsltStylesheetPtr cur = NULL;
  xmlDocPtr doc, res;
  xmlChar * doc_txt_ptr = NULL;
  int doc_txt_len;
  
  int currentEdit, currentLength;
  ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&currentEdit);
  HWND hCurrentEditView = getCurrentHScintilla(currentEdit);
  
  currentLength = (int) ::SendMessage(hCurrentEditView, SCI_GETLENGTH, 0, 0);
  
  char *data = new char[currentLength+1];
  if (!data) return;  // allocation error, abort check
  memset(data, '\0', currentLength+1);
  
  TextRange tr;
  tr.chrg.cpMin = 0;
  tr.chrg.cpMax = currentLength;
  tr.lpstrText = data;
  
  ::SendMessage(hCurrentEditView, SCI_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&tr)); 
  
  if (this->m_sXSLTFile.GetLength() <= 0) {
    Report::_printf_err(L"XSLT File missing. Cannot continue.");
    delete [] data;
    return;
  }
  
  // on commence par d�cortiquer la liste d'options
  // la liste doit avoir la forme suivante:
  //   variable1=value1;variable2=value2;variable3="value 3"
  i = -1;
  std::wstring options = this->m_sXSLTOptions, key, val;
  while ((i = getNextParam(options, i+1, &key, &val)) >= 0) {
    //Report::_printf_inf("%s = %s", key.c_str(), val.c_str());

    // param1=123 param2='abc' param3='xyz'

    params[nbparams] = new char[1+key.length()];
    Report::strcpy(params[nbparams], key.c_str());
    ++nbparams;

    params[nbparams] = new char[1+val.length()];
    Report::strcpy(params[nbparams], val.c_str());
    ++nbparams;

    if (nbparams >= MAX_PARAMS) {
      Report::_printf_err(L"Too many params.");
      delete [] data;
      cleanParams(params, nbparams);
      return;
    }
  }
  params[nbparams] = NULL;  // la liste de param�tres doit �tre 'NULL-terminated'

  pXmlSubstituteEntitiesDefault(1);
  //xmlLoadExtDtdDefaultValue = 1;

  std::wstring wfile(m_sXSLTFile);
  std::string file = Report::narrow(wfile);
  cur = pXsltParseStylesheetFile(reinterpret_cast<const xmlChar*>(file.c_str()));
  doc = pXmlReadMemory(data, currentLength, "noname.xml", NULL, 0);
  res = pXsltApplyStylesheet(cur, doc, (const char**)params);
  
  if (res != NULL && cur != NULL) {
    pXsltSaveResultToString(&doc_txt_ptr, &doc_txt_len, res, cur);

    if (doc_txt_ptr == NULL || doc_txt_ptr[0] == '\0') {
      Report::_printf_err(L"The transformation has generated empty document.");
    } else {
      ::SendMessage(nppData._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW); 
      currentLength = (int) ::SendMessage(hCurrentEditView, SCI_GETLENGTH, 0, 0);
      ::SendMessage(hCurrentEditView, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(doc_txt_ptr));
    }
  } else {
    Report::_printf_err(L"Unable to apply transformation on current source.");
  }

  pXsltFreeStylesheet(cur);
  pXmlFreeDoc(res);
  pXmlFreeDoc(doc);

  delete [] data;
  cleanParams(params, nbparams);

  cur = NULL;
  res = NULL;
  doc = NULL;
  data = NULL;

  pXsltCleanupGlobals();
  pXmlCleanupParser();
}

CString CXSLTransformDlg::ShowOpenFileDlg(CString filetypes)
{
  CFileDialog dlg(TRUE, NULL, NULL, NULL, filetypes);
  int ret = dlg.DoModal();
  if (ret == IDOK) {
    return dlg.GetPathName();
  }
  return "";
}

void CXSLTransformDlg::OnBtnBrowseXSLTFile() 
{
  CString ret = ShowOpenFileDlg("XSL Files (*.xsl)|*.xsl|XML Files (*.xml)|*.xml|All files (*.*)|*.*|");
  if (ret.GetLength()) GetDlgItem(IDC_EDIT_XSLTFILE)->SetWindowText(ret);
}